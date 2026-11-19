#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Author GoldSrc first- and third-person models for GoldCraft's flashlight."""
import argparse
import hashlib
import json
from math import cos, pi, sin, sqrt
from pathlib import Path
import struct
import subprocess

PALETTE = (
    (44, 51, 62),    # Anodized shell
    (24, 29, 36),    # Rubber grip
    (188, 143, 46),  # Identification band
    (34, 40, 48),    # Bezel
    (193, 221, 224), # Lens
    (51, 57, 47),    # Glove
    (72, 78, 63),    # Sleeve
)


def cylinder(parts, x0, x1, r0, r1, y, z, material, sides=12):
    rings = [
        [(x, y + radius * cos(2 * pi * i / sides), z + radius * sin(2 * pi * i / sides))
         for i in range(sides)]
        for x, radius in ((x0, r0), (x1, r1))
    ]
    back, front = rings
    for i in range(sides):
        j = (i + 1) % sides
        parts.extend(((material, back[i], back[j], front[j]),
                      (material, back[i], front[j], front[i])))
        if i >= 1 and j > i:
            parts.extend(((material, back[0], back[j], back[i]),
                          (material, front[0], front[i], front[j])))


def box(parts, lo, hi, material):
    points = [(x, y, z) for x in (lo[0], hi[0])
              for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    for a, b, c, d in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
                       (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        parts.extend(((material, points[a], points[b], points[c]),
                      (material, points[a], points[c], points[d])))


def skeleton(bone):
    return f'version 1\nnodes\n0 "{bone}" -1\nend\nskeleton\n'


def model_source(first_person):
    bone = "flashlight" if first_person else "Bip01 R Hand"
    pose = "0 0 0 0 0 0 0\n"
    y, z = (-5.5, -4.8) if first_person else (0, 0)
    x = 8 if first_person else -2
    parts = []
    for start, end, r0, r1, mat in (
        (0, .5, 1.05, 1.3, 0), (.5, 1.1, 1.3, 1.3, 0),
        (1.1, 5.4, 1.18, 1.18, 1), (5.4, 6, 1.18, 1.29, 0),
        (6, 6.52, 1.29, 1.42, 2), (6.52, 7.5, 1.42, 1.78, 0),
        (7.5, 8.25, 1.78, 1.78, 3), (8.25, 8.29, 1.54, 1.54, 4),
    ):
        cylinder(parts, x + start, x + end, r0, r1, y, z, mat)
    box(parts, (x + 4.4, y - .42, z + 1.05), (x + 5.15, y + .42, z + 1.37), 3)
    if first_person:
        # Original glove and sleeve support the light at its rubber grip.
        cylinder(parts, x - 6, x + 1.8, 2.45, 1.5, y - 1.35, z - 2.5, 6, 8)
        box(parts, (x + .45, y - 1.88, z - 2.35), (x + 4.1, y + .85, z - .45), 5)
        for i in range(4):
            finger = x + 1.1 + .7 * i
            box(parts, (finger, y + .28, z - 1.1), (finger + .57, y + 1.44, z + .46), 5)
        box(parts, (x + 2.35, y - 1.53, z - .62), (x + 3.95, y - .89, z + .8), 5)

    prefix = skeleton(bone)
    reference = [prefix, "time 0\n", pose, "end\ntriangles\n"]
    for material, a, b, c in parts:
        u = tuple(v - o for v, o in zip(b, a))
        v = tuple(v - o for v, o in zip(c, a))
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                  u[0] * v[1] - u[1] * v[0])
        length = sqrt(sum(component * component for component in normal))
        normal = tuple(component / length for component in normal)
        reference.append("flashlight.bmp\n")
        for point, tx, ty in ((a, 2, 2), (b, 5, 2), (c, 2, 5)):
            uv = ((material * 8 + tx) / 64, ty / 8)
            reference.append("0 " + " ".join(f"{component:.6f}" for component in (*point, *normal, *uv)) + "\n")
    reference.append("end\n")
    idle = prefix + "time 0\n" + pose + "time 1\n" + pose + "end\n"
    return "".join(reference), idle


def material_atlas():
    palette = b"".join(bytes((b, g, r, 0)) for r, g, b in PALETTE).ljust(1024, b"\0")
    pixels = bytes(min(x // 8, len(PALETTE) - 1) for x in range(64)) * 8
    offset = 14 + 40 + len(palette)
    header = struct.pack("<2sIHHI", b"BM", offset + len(pixels), 0, 0, offset)
    header += struct.pack("<IiiHHIIiiII", 40, 64, 8, 1, 8, 0, len(pixels), 0, 0, 256, 0)
    return header + palette + pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--studiomdl", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    root = Path(__file__).resolve().parents[1]
    if output.exists() or not output.is_relative_to(root / "build"):
        parser.error("Use a fresh output folder inside the workspace build directory")
    compiler = args.studiomdl.resolve(strict=True)
    scratch = output / "source"
    models = output / "models/goldcraft"
    scratch.mkdir(parents=True)
    models.mkdir(parents=True)
    (scratch / "flashlight.bmp").write_bytes(material_atlas())

    files = []
    for prefix in ("v", "p"):
        name = f"{prefix}_night_flashlight"
        reference, idle = model_source(prefix == "v")
        (scratch / "mesh.smd").write_text(reference, encoding="ascii")
        (scratch / "idle.smd").write_text(idle, encoding="ascii")
        script = scratch / f"{name}.qc"
        script.write_text(f'$modelname "{name}.mdl"\n$cd "."\n$origin 0 0 0 -90\n'
                          '$body "flashlight" "mesh"\n$sequence "idle" "idle" fps 15 loop\n', encoding="ascii")
        build = subprocess.run([str(compiler), script.name], cwd=scratch, capture_output=True, check=False)
        (output / f"{name}.log").write_bytes(build.stdout + build.stderr)
        path = scratch / f"{name}.mdl"
        if build.returncode or not path.is_file():
            raise RuntimeError(f"Studio model compilation failed. See {name}.log")
        data = path.read_bytes()
        if data[:8] != b"IDST\x0a\0\0\0" or struct.unpack_from("<i", data, 72)[0] != len(data):
            raise RuntimeError(f"Invalid GoldSrc model produced for {name}")
        installed = path.replace(models / path.name)
        files.append({
            "source": str(installed),
            "destination": installed.relative_to(output).as_posix(),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })

    (output / "resource-plan.json").write_text(
        json.dumps({"files": files}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
