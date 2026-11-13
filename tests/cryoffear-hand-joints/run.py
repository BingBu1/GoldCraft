#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real author and joint audit with a supplied paired CSO donor."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools"))
from cryoffear_studio import Studio


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT/"tools"/file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    if (output.exists() or not output.resolve().is_relative_to(ROOT/"build") or
            any(p.is_symlink() or p.is_junction() for p in (output, *output.parents))):
        raise ValueError("Use a fresh fixture directory inside workspace build")
    author = load("hand_author", "Prepare-CryOfFearHands.py")
    audit = load("hand_joint_audit", "Inspect-CryOfFearHandJoints.py")
    controls = [Path(__file__).resolve(), Path(author.__file__), Path(audit.__file__),
                ROOT/"tools/cryoffear_studio.py", args.model.resolve()]
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in controls}
    model = Studio.read(args.model)
    parts = audit.hands(model)
    used = sorted({bone for part in parts for kind in ("vertices", "normals") for bone in part[kind+"Bones"]})
    profile = dict(version=1, targetSha256=model.sha256, donorSha256=model.sha256,
                   bones=[dict(donor=model.bones[b]["name"], target=model.bones[b]["name"],
                               transform=[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]) for b in used])
    output.mkdir(parents=True)
    profile_path = output/"profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf8")
    data, _ = author.prepare(args.model, args.model, profile_path)
    identity = output/"identity.mdl"
    identity.write_bytes(data)
    baseline = audit.inspect(args.model, args.model, identity)
    assert baseline["originalFrames"] > 0 and all(x["crossBoneEdges"] > 0 for x in baseline["models"])
    assert all(len(x["worst"]) == 8 for x in baseline["models"])
    # A previously legal per-bone translation can tear actual connecting triangles.
    links = audit.connections(parts[0], parts[0], model, audit.poses(model))
    bone = parts[0]["verticesBones"][links[0][0]]
    row = next(x for x in profile["bones"] if x["donor"] == model.bones[bone]["name"])
    row["transform"][3] += 1000
    profile_path.write_text(json.dumps(profile), encoding="utf8")
    moved, _ = author.prepare(args.model, args.model, profile_path)
    displaced = output/"displaced.mdl"
    displaced.write_bytes(moved)
    changed = audit.inspect(args.model, args.model, displaced)
    assert changed["originalFrames"] == baseline["originalFrames"]
    assert max(x["maxRatio"] for x in changed["models"]) > 4*max(x["maxRatio"] for x in baseline["models"])
    rejected = []

    def reject(name, value, message):
        path = output/(name+".mdl")
        path.write_bytes(value)
        try:
            audit.inspect(args.model, args.model, path)
        except ValueError as error:
            assert message in str(error), (name, error)
            rejected.append(name)
        else:
            raise AssertionError(name+" was accepted")

    bad = bytearray(data)
    bad[244] ^= 1
    reject("changed-original", bad, "payload changed")
    candidate = Studio(data)
    bad = bytearray(data)
    bad[candidate.i(168)+32] ^= 1
    reject("changed-timing", bad, "sequence timing")
    changed_sequence = next(sequence for sequence in range(model.i(164))
                            if any(any(value for channel in model.channels(sequence, frame, blend) for value in channel)
                                   for blend in range(model.i(model.i(168)+sequence*176+120))
                                   for frame in range(model.i(model.i(168)+sequence*176+56))))
    bad = bytearray(data)
    sequence_at = candidate.i(168)+changed_sequence*176
    bad.extend(bytes(-len(bad) % 4))
    animation_at = len(bad)
    bad.extend(bytes(len(candidate.bones)*candidate.i(sequence_at+120)*12))
    struct.pack_into("<i", bad, sequence_at+124, animation_at)
    struct.pack_into("<i", bad, 72, len(bad))
    reject("changed-channels", bad, "channels changed")
    bad = bytearray(data)
    group = candidate.i(208)
    model_at = candidate.i(group+72)
    mesh_at = candidate.i(model_at+76)
    command = candidate.i(mesh_at+4)
    old = struct.unpack_from("<h", bad, command+2)[0]
    struct.pack_into("<h", bad, command+2, (old+1) % len(audit.hands(candidate)[0]["vertices"]))
    reject("changed-topology", bad, "topology")
    after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in controls}
    assert before == after
    receipt = dict(success=True, inputsUnchanged=True, inputs=before, originalFrames=baseline["originalFrames"],
                   detectsJointDisplacement=True, rejected=rejected, baseline=baseline, displaced=changed,
                   gameplay=False, performance=False)
    (output/"receipt.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf8")
    print(json.dumps({k: receipt[k] for k in ("success", "inputsUnchanged", "originalFrames", "detectsJointDisplacement", "rejected")}))


if __name__ == "__main__":
    main()
