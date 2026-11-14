#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit authored hand connectivity on every original CoF animation frame.

Reports geometric measurements, never a guessed grip/fidelity pass threshold.
No game process, resource installation or original model is modified.
"""
from __future__ import annotations

import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path

from cryoffear_studio import Studio, multiply, pose_matrix, transform

ROOT = Path(__file__).resolve().parents[1]


def poses(model, channels=None, *, bone_scope=None):
    if model.controllers and bone_scope is None:
        raise ValueError("Controlled poses need an explicit sampled state or independent bone scope")
    required = None if bone_scope is None else model.uncontrolled_scope(bone_scope)
    result = []
    for index, bone in enumerate(model.bones):
        if required is not None and index not in required:
            result.append(None)
            continue
        values = bone["pose"] if channels is None else [
            a + b*c for a, b, c in zip(bone["pose"], bone["scale"], channels[index])]
        local = pose_matrix(values)
        result.append(multiply(result[bone["parent"]], local) if bone["parent"] >= 0 else local)
    return result


def hands(model):
    groups = [part for part in model.bodyparts if part["name"] == "hands"]
    if len(groups) != 1 or len(groups[0]["models"]) != 2:
        raise ValueError("Expected the explicitly authored paired hand group")
    return groups[0]["models"]


def connections(source, candidate, donor, bind):
    if (len(source["vertices"]) != len(candidate["vertices"]) or
            len(source["normals"]) != len(candidate["normals"]) or
            len(source["meshes"]) != len(candidate["meshes"]) or
            any(a["triangles"] != b["triangles"] for a, b in zip(source["meshes"], candidate["meshes"]))):
        raise ValueError("Authored hand topology or UV indices changed")
    vertices, bones = source["vertices"], source["verticesBones"]
    world = [transform(bind[bone], point) for bone, point in zip(bones, vertices)]
    edges = set()
    for mesh in source["meshes"]:
        for triangle in mesh["triangles"]:
            for i in range(3):
                a, b = sorted((triangle[i][0], triangle[(i+1) % 3][0]))
                if bones[a] != bones[b]:
                    edges.add((a, b))
    return [(a, b, math.dist(world[a], world[b]),
             (donor.bones[bones[a]]["name"], donor.bones[bones[b]]["name"])) for a, b in sorted(edges)]


def inspect(target_path, donor_path, candidate_path):
    target = Studio.read(target_path, opaque_controllers=True)
    donor = Studio.read(donor_path)
    candidate = Studio.read(candidate_path, opaque_controllers=True)
    if candidate.data[244:len(target.data)] != target.data[244:]:
        raise ValueError("Original target payload changed")
    if candidate.bones != target.bones or candidate.attachments != target.attachments or candidate.i(164) != target.i(164):
        raise ValueError("Original skeleton, attachments or sequence count changed")
    source_parts, candidate_parts = hands(donor), hands(candidate)
    scope = {bone for part in candidate_parts for kind in ("vertices", "normals") for bone in part[kind+"Bones"]}
    target.uncontrolled_scope(scope)
    candidate.uncontrolled_scope(scope)
    bind = poses(donor)
    edges = [connections(a, b, donor, bind) for a, b in zip(source_parts, candidate_parts)]
    programs = []
    for part, links in zip(candidate_parts, edges):
        indices = sorted({vertex for a, b, _, _ in links for vertex in (a, b)})
        slots = {vertex: index for index, vertex in enumerate(indices)}
        points = [(part["verticesBones"][i], part["vertices"][i]) for i in indices]
        links = [(slots[a], slots[b], a, b, length, bones) for a, b, length, bones in links]
        programs.append((points, links))
    stats = [dict(samples=0, ratioSamples=0, ratioSum=0., maxRatio=None,
                  zeroSourceSamples=0, maxZeroSourceTargetLength=0., worst=[]) for _ in edges]
    frames = 0
    for sequence in range(target.i(164)):
        at, other = target.i(168) + sequence*176, candidate.i(168) + sequence*176
        for offset in (32, 36, 56, 120):
            if target.i(at+offset) != candidate.i(other+offset):
                raise ValueError("Original sequence timing, flags or blends changed")
        for blend in range(target.i(at+120)):
            for frame in range(target.i(at+56)):
                channels = target.channels(sequence, frame, blend)
                if channels != candidate.channels(sequence, frame, blend):
                    raise ValueError("Original animation channels changed")
                matrices = poses(target, channels, bone_scope=scope if target.controllers else None)
                for (points, links), state in zip(programs, stats):
                    world = [transform(matrices[bone], point) for bone, point in points]
                    ratios = []
                    for ia, ib, a, b, original_length, bones in links:
                        length = math.dist(world[ia], world[ib])
                        if not math.isfinite(length):
                            raise ValueError("Non-finite authored edge length")
                        state["samples"] += 1
                        if original_length == 0:
                            state["zeroSourceSamples"] += 1
                            state["maxZeroSourceTargetLength"] = max(state["maxZeroSourceTargetLength"], length)
                            continue
                        ratio = length/original_length
                        if not math.isfinite(ratio):
                            raise ValueError("Non-finite authored edge ratio")
                        ratios.append(ratio)
                        # Fixed eight records per model; no frame/vertex history cache.
                        if len(state["worst"]) < 8:
                            heapq.heappush(state["worst"], (ratio, sequence, blend, frame, a, b, original_length, length, bones))
                        elif ratio >= state["worst"][0][0]:
                            record = (ratio, sequence, blend, frame, a, b, original_length, length, bones)
                            if record > state["worst"][0]:
                                heapq.heapreplace(state["worst"], record)
                    state["ratioSamples"] += len(ratios)
                    state["ratioSum"] += math.fsum(ratios)
                frames += 1
    for state, links in zip(stats, edges):
        state["crossBoneEdges"] = len(links)
        state["meanRatio"] = state.pop("ratioSum")/state["ratioSamples"] if state["ratioSamples"] else None
        state["worst"] = [dict(ratio=row[0], sequence=row[1], blend=row[2], frame=row[3],
                               vertices=list(row[4:6]), sourceLength=row[6], targetLength=row[7], bones=list(row[8]))
                          for row in sorted(state["worst"], reverse=True)]
        state["maxRatio"] = state["worst"][0]["ratio"] if state["worst"] else None
    return dict(targetSha256=target.sha256, donorSha256=donor.sha256, candidateSha256=candidate.sha256,
                originalFrames=frames, originalPayloadPreserved=True, originalChannelsPreserved=True,
                models=stats, visualAccepted=False, gameplayAccepted=False, performanceAccepted=False,
                scope="Offline cross-bone edge lengths against donor bind geometry; scale differences and grip require author review")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("target", "donor", "candidate", "output"):
        parser.add_argument("--"+name, type=Path, required=True)
    args = parser.parse_args()
    destination = args.output.absolute()
    if (destination.exists() or not destination.resolve().is_relative_to(ROOT/"build") or
            any(path.is_symlink() or path.is_junction() for path in (destination, *destination.parents))):
        raise ValueError("Use a fresh report path inside workspace build")
    report = inspect(args.target, args.donor, args.candidate)
    for name, path in (("target", args.target), ("donor", args.donor), ("candidate", args.candidate)):
        if hashlib.sha256(path.read_bytes()).hexdigest() != report[name+"Sha256"]:
            raise ValueError("Audit input changed: "+name)
    report["inputsUnchanged"] = True
    report["toolSha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2)+"\n", encoding="utf8")
    print(json.dumps(dict(originalFrames=report["originalFrames"], candidateSha256=report["candidateSha256"],
                          models=[{key: state[key] for key in ("samples", "crossBoneEdges", "meanRatio", "maxRatio")}
                                  for state in report["models"]], inputsUnchanged=True, visualAccepted=False)))


if __name__ == "__main__":
    main()
