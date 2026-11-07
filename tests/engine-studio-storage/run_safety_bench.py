#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure actual character safety passes with the existing x86 SDK build flags.

Preparation reads small headers and writes only a plan/list. Compilation and
execution require a separately coordinated build window. Models and source
trees are never copied. Results are offline validation costs, not map timings.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

from run import identity, write_json
from run_resources import span

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/engine-studio-safety-bench")
    parser.add_argument("--build-plan", type=Path, default=ROOT / "build/engine-studio-storage-tests/plan.json")
    parser.add_argument("--game-root", type=Path, default=ROOT / "sandbox/cs-server/Half-Life/cstrike")
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    output = args.out.resolve()
    game = args.game_root.resolve(strict=True)
    if not output.is_relative_to(ROOT / "build"):
        parser.error("output must stay under the workspace build directory")
    if not game.is_relative_to(ROOT / "sandbox") or not 1 <= args.rounds <= 10:
        parser.error("game root must be a workspace sandbox; rounds must be 1..10")
    base = json.loads(args.build_plan.read_text(encoding="utf-8-sig"))
    compile_command = next(row["argv"] for row in base["commands"] if row["name"] == "compile-0")
    flags = compile_command[:compile_command.index("/c")]
    if "--target=i686-pc-windows-msvc" not in flags or "/clang:-O3" not in flags or "/arch:AVX2" not in flags:
        raise ValueError("build plan is not the expected x86 O3 AVX2 fixture configuration")
    environment_path = Path(base["environmentInput"]["path"])
    catalog_path = game / "models/csol/characters.json"
    catalog = json.loads(catalog_path.read_text(encoding="utf-8-sig"))
    models = []
    for alias in sorted({row["model"] for row in catalog["characters"]}):
        if not alias or any(char not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for char in alias):
            raise ValueError("unexpected model alias")
        path = (game / "models/player" / alias / f"{alias}.mdl").resolve(strict=True)
        if not path.is_relative_to(game):
            raise ValueError("model escaped selected game directory")
        with path.open("rb") as stream:
            header = stream.read(244)
            if len(header) != 244 or header[:8] != b"IDST\x0a\0\0\0":
                raise ValueError(f"not a decoded studio model: {path}")
            descriptors = struct.unpack_from("<I", header, 168)[0]
            stream.seek(max(0, descriptors - 32))
            tagged = descriptors >= 32 and stream.read(8) == b"CSEQDTA1"
        # This benchmark intentionally measures the actual installed models.
        # An adjacent SEQ would require the real decoder/append entry first.
        if path.with_name(path.stem + "1.seq").exists():
            raise ValueError(f"adjacent SEQ requires a full loader benchmark: {path}")
        stat = path.stat()
        models.append({"path": str(path), "bytes": stat.st_size, "mtimeNs": stat.st_mtime_ns, "tagged": tagged})
    output.mkdir(parents=True, exist_ok=True)
    model_list = output / "models.txt"
    model_list.write_text("\n".join(row["path"] for row in models) + "\n", encoding="utf-8")
    fixture = HERE / "safety_bench.cpp"
    # Only two local function extracts are generated. The production headers
    # and all SDK types are included directly; no alternate source tree exists.
    safety_source = ROOT / "native/csol/sequence_safety.hpp"
    original = safety_source.read_bytes()
    begin, end = span(original, "inline bool Animation(")
    animation = original[begin:end].replace(b"inline bool Animation(", b"inline bool CandidateAnimation(", 1)
    old = b"""        for(auto at=begin;at<begin+length;++at)
        {
            if(roles[at] && roles[at]!=role)return false;
            roles[at]=role;
        }"""
    animation = animation.replace(b"\r\n", b"\n")
    if animation.count(old) != 1:
        raise ValueError("original role marking changed; candidate needs review")
    replacement = b"""        if(std::memchr(roles.data()+begin,3-role,length))return false;
        std::memset(roles.data()+begin,role,length);"""
    animation = animation.replace(old, replacement, 1)
    candidate_animation = output / "animation-candidate.inc"
    candidate_animation.write_bytes(b"namespace csol::sequence_safety_detail {\n" + animation + b"\n}\n")
    storage_source = ROOT / "native/engine/csol_studio_storage.h"
    storage_bytes = storage_source.read_bytes()
    storage_begin, storage_end = span(storage_bytes, "inline StudioStorage PlanStudioStorage(")
    storage = storage_bytes[storage_begin:storage_end]
    if storage.count(b"sequence_safety_detail::Animation(") != 1:
        raise ValueError("storage animation dispatch changed")
    storage = storage.replace(b"PlanStudioStorage(", b"CandidatePlanStudioStorage(", 1)
    storage = storage.replace(b"sequence_safety_detail::Animation(", b"sequence_safety_detail::CandidateAnimation(", 1)
    candidate_storage = output / "storage-candidate.inc"
    candidate_storage.write_bytes(b"namespace csol {\n" + storage + b"\n}\n")
    flags.append(f"/I{output}")
    obj, exe = output / "bench.obj", output / "bench.exe"
    commands = [
        {"name": "compile", "argv": [*flags, "/c", str(fixture), f"/Fo{obj}"]},
        {"name": "link", "argv": [flags[0], "--target=i686-pc-windows-msvc", "-fuse-ld=lld", "/MT",
                                    str(obj), f"/Fe{exe}", "/link", "/OPT:REF", "/OPT:ICF", "/OPT:LLDLTO=3", "/threads:1"]},
        {"name": "bench", "argv": [str(exe), str(model_list), str(args.rounds)]},
    ]
    source_paths = [fixture, Path(__file__), HERE / "run.py", ROOT / "native/csol/sequence_owners.hpp",
                    safety_source, storage_source, HERE / "run_resources.py"]
    plan = {"commands": commands, "sources": [identity(path) for path in source_paths],
            "basePlan": identity(args.build_plan), "environment": identity(environment_path),
            "catalog": identity(catalog_path), "modelList": identity(model_list), "models": models,
            "candidateFunctions": [{"source": identity(safety_source), "range": [begin, end],
                                     "originalSha256": hashlib.sha256(original[begin:end]).hexdigest(),
                                     "generated": identity(candidate_animation)},
                                    {"source": identity(storage_source), "range": [storage_begin, storage_end],
                                     "originalSha256": hashlib.sha256(storage_bytes[storage_begin:storage_end]).hexdigest(),
                                     "generated": identity(candidate_storage)}],
            "prepareOnly": args.prepare_only, "liveGame": False, "sourceTreesCopied": False,
            "note": "Each model is read once, then all three actual stages run consecutively per repetition. OS caches are not flushed."}
    write_json(output / "plan.json", plan)
    if args.prepare_only:
        print(json.dumps({"prepared": str(output / "plan.json"), "models": len(models), "compiled": False}))
        return 0

    result = {"success": False, "liveGame": False, "commands": []}
    began = time.monotonic()
    try:
        # Hash real model inputs outside the measured executable. These reads
        # warm OS caches, so the output never claims a cold I/O measurement.
        model_identities = [identity(Path(row["path"])) for row in models]
        result["models"] = model_identities
        environment = dict(os.environ)
        overrides = json.loads(environment_path.read_text(encoding="utf-8-sig"))
        if not isinstance(overrides, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in overrides.items()):
            raise ValueError("build environment is not a string dictionary")
        environment.update(overrides)
        environment["VSLANG"] = "1033"
        for command in commands:
            started = time.monotonic()
            ran = subprocess.run(command["argv"], cwd=output, env=environment, capture_output=True,
                                 text=True, timeout=240, creationflags=subprocess.CREATE_NO_WINDOW)
            log = output / f"{command['name']}.log"
            log.write_text(ran.stdout + ran.stderr, encoding="utf-8")
            result["commands"].append({"name": command["name"], "exit": ran.returncode,
                                       "seconds": time.monotonic() - started, "log": identity(log)})
            if ran.returncode:
                raise RuntimeError(f"{command['name']} exited {ran.returncode}; see {log}")
        headers = {Path(line.split("including file:", 1)[1].strip()).resolve()
                   for line in (output / "compile.log").read_text(encoding="utf-8").splitlines()
                   if "including file:" in line}
        result["actualIncludes"] = [identity(path) for path in sorted(headers)]
        samples, observed_models, complete = [], [], None
        for line in (output / "bench.log").read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if fields[0] == "model":
                observed_models.append({"index": int(fields[1]), "bytes": int(fields[2]),
                                        "sequences": int(fields[3]), "warmReadSeconds": float(fields[4])})
            elif fields[0] == "sample":
                samples.append({"index": int(fields[1]), "round": int(fields[2]),
                                "readSafety": float(fields[3]), "guardPrepare": float(fields[4]),
                                "storagePlan": float(fields[5]), "retainedBytes": int(fields[6]),
                                "candidateStoragePlan": float(fields[7])})
            elif fields[0] == "boundaries":
                result["rleBoundaries"] = int(fields[1])
            elif fields[0] == "complete":
                complete = (int(fields[1]), int(fields[2]))
            else:
                raise ValueError("unexpected benchmark output")
        if complete != (len(models), args.rounds) or len(observed_models) != len(models):
            raise ValueError("incomplete model coverage")
        if len(samples) != len(models) * args.rounds or {(row["index"], row["round"]) for row in samples} != {
                (index, round_) for index in range(len(models)) for round_ in range(args.rounds)}:
            raise ValueError("incomplete sample coverage")
        result["modelSummaries"], result["samples"] = observed_models, samples
        result["totalsByRound"] = [{"round": round_, **{
            stage: sum(row[stage] for row in samples if row["round"] == round_)
            for stage in ("readSafety", "guardPrepare", "storagePlan", "candidateStoragePlan")}} for round_ in range(args.rounds)]
        result["warmReadSeconds"] = sum(row["warmReadSeconds"] for row in observed_models)
        checked = [*plan["sources"], plan["catalog"], plan["modelList"],
                   *(row["generated"] for row in plan["candidateFunctions"])]
        result["sourceInputsUnchanged"] = all(identity(Path(row["path"])) == row for row in checked)
        result["modelsUnchanged"] = all(identity(Path(row["path"])) == row for row in model_identities)
        result["executable"] = identity(exe)
        pe = exe.read_bytes()
        offset = struct.unpack_from("<I", pe, 0x3C)[0]
        result["x86"] = pe[offset:offset+4] == b"PE\0\0" and struct.unpack_from("<H", pe, offset+4)[0] == 0x14C
        result["success"] = result["sourceInputsUnchanged"] and result["modelsUnchanged"] and result["x86"]
    except Exception as error:
        result["error"] = str(error)
    finally:
        result["seconds"] = time.monotonic() - began
        write_json(output / "result.json", result)
    print(json.dumps({key: result[key] for key in ("success", "seconds")}))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
