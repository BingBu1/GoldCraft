#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the CoF FAMAS third-person model from its world mesh and the stock CS hand rig."""
import argparse
from importlib import import_module
import json
import math
from pathlib import Path
from tempfile import TemporaryDirectory

from cryoffear_studio import Studio, inverse, multiply

generator = import_module("Generate-CryOfFearPlayerModel")
attachment = import_module("Author-MissingStudioAttachment")

WORLD_SHA256 = "dd9ce3c9127d9d3aedb8c282c1154cf92a3eba28f66728fa8694250e6edcdfeb"
REFERENCE_SHA256 = "2ba517cebe96ae42bed2c872cde6e0d83deb3bde9223707838eddc3660d3ac72"

# Vertices are in local p_famas coordinates in both inputs. In the world model
# the barrel and grip are in different Studio bodyparts, both drawn at body 0.
WORLD_MUZZLE = (836, 835, 861, 828, 824, 823, 864, 816, 808, 807, 865, 840)
WORLD_BARREL_REAR = (833, 832, 830, 826, 821, 820, 818, 814, 809, 812, 842, 838)
REFERENCE_MUZZLE = (59, 60, 62, 63)
REFERENCE_BARREL_REAR = (67, 58, 61, 68)


def center(vertices, indices):
    return tuple(sum(vertices[i][axis] for i in indices) / len(indices) for axis in range(3))


def direction(end, start):
    delta = tuple(a - b for a, b in zip(end, start))
    length = math.hypot(*delta)
    if length < 1e-8:
        raise ValueError("Degenerate FAMAS calibration axis")
    return tuple(x / length for x in delta)


def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0])


def grip_frame(barrel, front, rear, width, grip, corners):
    forward = direction(center(barrel, front), center(barrel, rear))
    transverse = direction(barrel[width[0]], barrel[width[1]])
    up = direction(cross(forward, transverse), (0, 0, 0))
    side = cross(up, forward)
    origin = center([center(grip, indices) for indices in corners], range(4))
    return [[forward[i], side[i], up[i], origin[i]] for i in range(3)]


def build(world, reference):
    if world.sha256 != WORLD_SHA256 or reference.sha256 != REFERENCE_SHA256:
        raise ValueError("The original CoF world model and stock CS P model must match their reviewed identities")
    barrel, grip = [body["models"][0]["vertices"] for body in world.bodyparts]
    stock = reference.bodyparts[0]["models"][0]["vertices"]
    source_frame = grip_frame(barrel, WORLD_MUZZLE, WORLD_BARREL_REAR, (807, 828), grip,
                              ((432,), (383,), (490, 794), (388,)))
    stock_frame = grip_frame(stock, REFERENCE_MUZZLE, REFERENCE_BARREL_REAR, (63, 60), stock,
                             ((51, 54), (57,), (52, 53), (55, 56)))
    transform = multiply(stock_frame, inverse(source_frame))
    sample = dict(index=0, label="idle", frame=0, blend=0)
    recipe = Path(__file__).resolve()
    calibration = dict(
        schema=1, status="candidate", attachments="all",
        evidence=dict(path=str(recipe), sha256=generator.digest(recipe.read_bytes())),
        source=dict(sha256=WORLD_SHA256, bone="p_famas", sequence=sample),
        target=dict(sha256=REFERENCE_SHA256, bone="p_famas", handBone="Bip01 R Hand", sequence=sample),
        sourceVertexBones=["p_famas"], sourceAnchorToTargetBone=transform,
        textures=[dict(name=t["name"], sha256=t["sha256"]) for t in world.textures])
    data, geometry = generator.generate(world, reference, calibration, candidate=True)
    # The generator compacts vertices in first-use order. Follow the actual
    # emitted corners across the matched triangles to recover the full rim.
    source_mesh = world.bodyparts[0]["models"][0]["meshes"][0]
    built_mesh = Studio(data).bodyparts[0]["models"][0]["meshes"][0]
    remap = {}
    for old, new in zip(source_mesh["triangles"], built_mesh["triangles"], strict=True):
        for a, b in zip(old, new):
            if remap.setdefault(a[0], b[0]) != b[0]:
                raise ValueError("Ambiguous muzzle vertex after Studio mesh compilation")
    profile = dict(schema=1, status="candidate", modelSha256=geometry["outputSha256"],
                   bodypart=0, model=0, bone="p_famas", sequence=0, frame=0, blend=0,
                   name="muzzle", type=0, vectors=[[0, 0, 0]]*3,
                   landmarkVertices=[remap[i] for i in WORLD_MUZZLE])
    model, muzzle = attachment.author(data, profile)
    return model, dict(source=world.sha256, reference=reference.sha256, recipe=calibration["evidence"]["sha256"],
                       sourceToReference=transform, sourceGripFrame=source_frame, referenceGripFrame=stock_frame,
                       triangles=geometry["selectedTriangles"], excludedTriangles=geometry["excludedTriangles"],
                       bodyMap=geometry["bodyMap"], textures=geometry["textures"], attachment=muzzle,
                       sha256=generator.digest(model), bytes=len(model))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world-model", type=Path, required=True)
    parser.add_argument("--cs-player-model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory under build")
    args = parser.parse_args()
    output = generator.ordinary(args.output)
    root = generator.ordinary(generator.ROOT / "build")
    if root not in output.parents or output.exists():
        parser.error("Choose a fresh directory under build")
    data, receipt = build(Studio.read(generator.ordinary(args.world_model)),
                          Studio.read(generator.ordinary(args.cs_player_model)))
    output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix=".cof-famas-", dir=output.parent) as temporary:
        stage = Path(temporary) / "model"
        stage.mkdir()
        (stage / "p_famas.mdl").write_bytes(data)
        (stage / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf8")
        stage.rename(output)
    print(f"{receipt['sha256']}  {output / 'p_famas.mdl'}")


if __name__ == "__main__":
    main()
