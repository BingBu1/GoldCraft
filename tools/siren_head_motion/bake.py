"""Build a five-sequence GoldSrc monster MDL from the converted Siren Head body.

Run bake.py BODY.mdl --studiomdl PATH --output OUTPUT.mdl. Use --smd-dir to
keep the editable SMD animation tracks. The model's origin is at floor level.
"""

import argparse
import os
from pathlib import Path
import struct
from tempfile import TemporaryDirectory
import subprocess

import numpy as np
from PIL import Image
from studio import Model, integer as i, matrices, accumulate
from rig import Rig
from footfalls import LEFT_STEP, RIGHT_STEP, STOMP_LOAD, STOMP_HIT, write_footfalls
from motion import FRAMES, FPS, IMPACT, STRIDE
from reactions import DEATH_BRACE, DEATH_FALL, DEATH_HORN, DEATH_RUNDOWN, DEATH_VOICE, FLINCH, write_reactions
from shockwave import EVENT_ID, EVENT_OPTION, RELEASE_FRAME, WINDUP_FRAME
from sound import SOUND, WINDUP_SOUND, render_windup, write_wave

# The compiler links new streams into the known reference's contiguous mesh prefix.
SOURCE_SHA256 = "675191b5f8dbf803e974e42779e2212c06d63b7efc44001fa2a063a539d091a3"
ACTIVITIES = {
    "walk": "ACT_WALK",
    "stomp": "ACT_MELEE_ATTACK1",
    "flinch": "ACT_SMALL_FLINCH",
    "death": "ACT_DIESIMPLE",
    "shockwave": "ACT_RANGE_ATTACK1",
}


# Use separate stock sound channels so a resumed walking cycle leaves the attack tail audible.
BODY_SOUND = 1004
VOICE_SOUND = 1008
SEQUENCE_EVENTS = {
    "walk": ((0, VOICE_SOUND, LEFT_STEP), ((FRAMES["walk"] - 1) // 2, VOICE_SOUND, RIGHT_STEP)),
    "stomp": ((12, VOICE_SOUND, STOMP_LOAD), (IMPACT, BODY_SOUND, STOMP_HIT)),
    "flinch": ((0, VOICE_SOUND, FLINCH),),
    # Start on both channels so a fatal hit can interrupt either a grunt or a siren attack.
    "death": ((0, VOICE_SOUND, DEATH_VOICE), (0, BODY_SOUND, DEATH_RUNDOWN),
              (48, BODY_SOUND, DEATH_BRACE), (96, BODY_SOUND, DEATH_FALL),
              (125, VOICE_SOUND, DEATH_HORN)),
    "shockwave": ((WINDUP_FRAME, BODY_SOUND, WINDUP_SOUND), (RELEASE_FRAME, EVENT_ID, EVENT_OPTION),
                  (RELEASE_FRAME, BODY_SOUND, SOUND)),
}


def smd_frame(v):
    return "".join(str(j) + " " + " ".join(f"{n:.8f}" for n in row) + "\n" for j, row in enumerate(v))


def write_source(rig, src):
    m = rig.model
    src.mkdir(parents=True, exist_ok=True)
    header = (
        "version 1\nnodes\n"
        + "".join(f'{j} "{name}" {m.parents[j]}\n' for j, name in enumerate(m.names))
        + "end\nskeleton\n"
    )
    ref = header + "time 0\n" + smd_frame(m.values) + "end\ntriangles\n"
    # Give StudioMDL a small reference mesh touching every bone; its compiled animation streams
    # are linked by bone name into the existing mesh, whose skin, normals and UVs are preserved.
    with (src / "skeleton.smd").open("w", encoding="ascii", newline="\n") as f:
        f.write(ref)
        for j, mat in enumerate(rig.bind):
            f.write("bind.bmp\n")
            for offset, uv in [([0, 0, 0], (0, 0)), ([0, 0, 0.5], (0, 1)), ([0, 0.5, 0], (1, 0))]:
                pos = mat[:3, 3] + mat[:3, :3] @ offset
                f.write(f"{j} {pos[0]:.7f} {pos[1]:.7f} {pos[2]:.7f} 1 0 0 {uv[0]} {uv[1]}\n")
        f.write("end\n")
    Image.new("P", (16, 16), 0).save(src / "bind.bmp")
    qc = [
        '$modelname "motions.mdl"',
        '$cd "."',
        '$cdtexture "."',
        "$origin 0 0 0 -90",
        '$body "skeleton" "skeleton"',
    ]
    worlds = {}
    for name, count in FRAMES.items():
        world = np.stack([rig.pose(name, j) for j in range(count)])
        worlds[name] = world
        raw = rig.raw(world)
        # Remove full turns introduced by the Euler representation before SMD export.
        angles = raw[:, :, 3:]
        raw[:, :, 3:] = np.unwrap(angles, axis=0)
        with (src / (name + ".smd")).open("w", encoding="ascii", newline="\n") as f:
            f.write(header)
            for j, frame in enumerate(raw):
                f.write(f"time {j}\n" + smd_frame(frame))
            f.write("end\n")
        events = "".join(
            f' {{ event {event} {frame} "{option}" }}'
            for frame, event, option in SEQUENCE_EVENTS.get(name, ())
        )
        qc.append(
            f'$sequence "{name}" "{name}" fps {FPS} {ACTIVITIES[name]} 1'
            + (" loop" if name == "walk" else "")
            + events
        )
    (src / "motions.qc").write_text("\n".join(qc) + "\n", encoding="ascii")
    return worlds


def link_model(rig, compiled, worlds, staged):
    orig = rig.model
    src = orig.b
    donor = Model(compiled)
    b = donor.b
    assert set(donor.seq) == set(FRAMES)
    assert set(donor.names) == set(orig.names), (donor.names, orig.names)
    assert i(b, 172) == 1 and i(b, i(b, 176) + 100) == 0
    inds = [donor.names.index(n) for n in orig.names]
    for a, idx in enumerate(inds):
        p = donor.parents[idx]
        assert (orig.parents[a] == -1 and p == -1) or (
            p >= 0 and orig.names[orig.parents[a]] == donor.names[p]
        )
    # The reference identified by SOURCE_SHA256 put all geometry before its final skeleton, and all texture,
    # palette and skin-family data together after the sequences. Retain only these used blocks.
    mesh_end = i(src, 144)
    tex_start = i(src, 184)
    out = bytearray(src[:mesh_end])

    def append(data):
        out.extend(bytes((-len(out)) % 4))
        offset = len(out)
        out.extend(data)
        return offset

    def seti(at, *v):
        struct.pack_into("<" + "i" * len(v), out, at, *v)

    bon = bytearray(src[mesh_end : mesh_end + len(orig.names) * 112])
    for j, k in enumerate(inds):
        d = i(b, 144) + k * 112
        bon[j * 112 + 64 : j * 112 + 112] = b[d + 64 : d + 112]
    seti(140, len(orig.names), append(bon))
    hboxes = i(src, 156)
    seti(156, hboxes, append(src[i(src, 160) : i(src, 160) + 32 * hboxes]))
    descriptors = append(bytes(176 * len(FRAMES)))
    seti(164, len(FRAMES), descriptors)
    groups = bytearray(104)
    groups[:8] = b"default\0"
    seti(172, 1, append(groups))
    for n, (name, count) in enumerate(FRAMES.items()):
        s = donor.seq[name]
        q = descriptors + 176 * n
        out[q : q + 176] = b[s : s + 176]
        assert i(b, s + 56) == count and i(b, s + 120) == 1 and i(b, s + 60) == 0
        stream = bytearray(12 * len(inds))
        for j, k in enumerate(inds):
            srcanim = i(b, s + 124) + 12 * k
            for channel, relative in enumerate(struct.unpack_from("<6H", b, srcanim)):
                if not relative:
                    continue
                a = srcanim + relative
                stop = a
                frames = 0
                while frames < count:
                    valid, total = struct.unpack_from("<BB", b, stop)
                    assert 0 < valid <= total
                    frames += total
                    stop += 2 + 2 * valid
                offset = len(stream) - j * 12
                assert 0 < offset <= 65535
                struct.pack_into("<H", stream, j * 12 + 2 * channel, offset)
                stream.extend(b[a:stop])
        seti(q + 124, append(stream))
        for k in (52, 64, 156):
            seti(q + k, 0)
        event_count, event_table = struct.unpack_from("<ii", b, s + 48)
        assert event_count == len(SEQUENCE_EVENTS.get(name, ()))
        if event_count:
            seti(q + 52, append(b[event_table : event_table + 76 * event_count]))
        if name == "walk":
            # NPCs derive nominal ground speed from linear movement while the feet animate in place.
            struct.pack_into("<3f", out, q + 76, STRIDE, 0, 0)
        # Bound all rendered vertices across the clip, not the compiler's proxy skeleton.
        extrema = []
        for frame in worlds[name]:
            vertices = rig.surface.skin(frame)
            extrema.append((vertices.min(axis=0), vertices.max(axis=0)))
        lo = np.min([pair[0] for pair in extrema], axis=0) - 0.55
        hi = np.max([pair[1] for pair in extrema], axis=0) + 0.55
        # A conservative sphere contribution covers between-key slerp arcs on the long arms.
        lo -= 1.25
        hi += 1.25
        struct.pack_into("<6f", out, q + 96, *lo, *hi)
    seti(148, 0, 0)
    attachments = bytearray()
    for label, mouth in zip(("siren_left", "siren_right"), rig.mouths):
        attachments.extend(
            struct.pack(
                "<32sii12f", label.encode(), 0, orig.names.index("Bip01 Head"), *mouth, *np.eye(3).ravel()
            )
        )
    seti(212, len(rig.mouths), append(attachments))
    seti(236, 0, 0)
    struct.pack_into("<9f", out, 76, 6, 0, 78, -12, -12, 0, 12, 12, 89)
    struct.pack_into("<6f", out, 112, 0, 0, 0, 0, 0, 0)
    tex = append(src[tex_start:])
    delta = tex - tex_start
    for field in (184, 188, 200):
        seti(field, i(src, field) + delta)
    for k in range(i(src, 180)):
        q = tex + 80 * k
        seti(q + 76, i(out, q + 76) + delta)
    seti(72, len(out))
    staged.write_bytes(out)
    actual = Model(staged)
    for name in FRAMES:
        w = accumulate(matrices(actual.raw(name)), actual.parents)
        d = np.max(np.linalg.norm(w[:, :, :3, 3] - worlds[name][:, :, :3, 3], axis=-1))
        rd = np.einsum("fbij,fbji->fb", w[:, :, :3, :3].transpose(0, 1, 3, 2), worlds[name][:, :, :3, :3])
        a = float(np.degrees(np.arccos(np.clip((rd - 1) / 2, -1, 1))).max())
        print(name, FRAMES[name], round(d, 5), round(a, 5), flush=True)
        assert d < 0.09 and a < 0.10, (name, d, a)


def size_for_game(authored):
    """Scale the articulated MDL in model space, including its server-side hitboxes."""
    result = bytearray(authored)
    # The first standing pose is 91 units tall. At 4.75x it stands six CS players high.
    def distances(offset, count):
        source = np.frombuffer(authored, dtype="<f4", count=count, offset=offset)
        dest = np.frombuffer(result, dtype="<f4", count=count, offset=offset)
        np.multiply(source, np.float32(4.75), out=dest)

    distances(76, 15)  # Eye, movement hull and clipping bounds.
    for bone in range(i(authored, 140)):
        at = i(authored, 144) + 112 * bone
        distances(at + 64, 3)  # Default translation.
        distances(at + 88, 3)  # Translation per animation stream unit.
    for hitbox in range(i(authored, 156)):
        distances(i(authored, 160) + 32 * hitbox + 8, 6)
    for sequence in range(i(authored, 164)):
        at = i(authored, 168) + 176 * sequence
        distances(at + 76, 3)  # Linear movement per cycle.
        distances(at + 96, 6)
    for part in range(i(authored, 204)):
        body = i(authored, 208) + 76 * part
        for index in range(i(authored, body + 64)):
            model = i(authored, body + 72) + 112 * index
            distances(model + 68, 1)  # Bounding radius.
            distances(i(authored, model + 88), 3 * i(authored, model + 80))
    for attachment in range(i(authored, 212)):
        distances(i(authored, 216) + 88 * attachment + 40, 3)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("body", type=Path)
    parser.add_argument("--studiomdl", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--smd-dir", type=Path)
    parser.add_argument("--sound-dir", type=Path, help="Game sound directory for the creature WAVs")
    args = parser.parse_args()
    original = Model(args.body)
    if original.studio.sha256 != SOURCE_SHA256:
        parser.error("This rig is authored for the converted Siren Head body with SHA-256 " + SOURCE_SHA256)
    output = args.output.resolve()
    if output == args.body.resolve():
        parser.error("Choose an output path distinct from the reference model")
    output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=output.parent) as scratch:
        temp = Path(scratch)
        src = temp / "smd"
        rig = Rig(original)
        worlds = write_source(rig, src)
        subprocess.run([str(args.studiomdl.resolve(strict=True)), "motions.qc"], cwd=src, check=True)
        staged = temp / "siren_head.mdl"
        link_model(rig, src / "motions.mdl", worlds, staged)
        if args.smd_dir:
            args.smd_dir.mkdir(parents=True, exist_ok=True)
            for name in FRAMES:
                content = (src / (name + ".smd")).read_bytes()
                (args.smd_dir / (name + ".smd")).write_bytes(content)
        if args.sound_dir:
            write_wave(args.sound_dir / WINDUP_SOUND, render_windup())
            write_wave(args.sound_dir / SOUND)
            write_footfalls(args.sound_dir)
            write_reactions(args.sound_dir)
        staged.write_bytes(size_for_game(staged.read_bytes()))
        os.replace(staged, output)


if __name__ == "__main__":
    main()
