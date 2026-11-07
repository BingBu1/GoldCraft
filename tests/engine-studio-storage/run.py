#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual x86 Studio loader/guard using synthetic model inputs.

Requires a Windows C++ developer environment and an AVX2-capable host.
Direct project sources are compiled. Only generated include adapters, objects,
results and local identity manifests stay in the reusable --out directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time


HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parents[1]
LOCAL_INCLUDE = re.compile(rb'^\s*#\s*include\s*"([^"\r\n]+)"', re.MULTILINE)
DEPENDENCY_UNITS = (
    "dependencies", "model_codec", "texture_model", "model_json", "events", "model_source",
)
NATIVE_INCLUDE_DIRS = ("native/engine", "native/csol", "native/include")


def identity(path: Path) -> dict:
    path = path.resolve(strict=True)
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def function_span(data: bytes, name: str) -> tuple[int, int]:
    """Find one complete named function, ignoring braces in comments/strings."""
    marker = f"void {name}(".encode()
    if data.count(marker) != 1:
        raise ValueError(f"expected one definition of {name}")
    start = data.index(marker)
    cursor = data.index(b"{", start)
    depth, state = 0, "code"
    while cursor < len(data):
        current, pair = data[cursor:cursor + 1], data[cursor:cursor + 2]
        if state == "line":
            if current == b"\n":
                state = "code"
        elif state == "comment":
            if pair == b"*/":
                state = "code"
                cursor += 1
        elif state in ('"', "'"):
            if current == b"\\":
                cursor += 1
            elif current == state.encode():
                state = "code"
        elif pair in (b"//", b"/*"):
            state = "line" if pair == b"//" else "comment"
            cursor += 1
        elif current in (b'"', b"'"):
            state = current.decode()
        elif current == b"{":
            depth += 1
        elif current == b"}":
            depth -= 1
            if depth == 0:
                return start, cursor + 1
        cursor += 1
    raise ValueError(f"unterminated definition of {name}")


class Inputs:
    """Record exact project inputs without copying source trees."""

    def __init__(self, source_roots: list[Path]):
        self.roots = list(dict.fromkeys(source_roots))
        self.records: dict[str, dict] = {}

    def add(self, original: Path, relative: str = "") -> Path:
        original = original.resolve(strict=True)
        key = str(original)
        if key in self.records:
            return original
        self.records[key] = identity(original)
        for match in LOCAL_INCLUDE.finditer(original.read_bytes()):
            include = match.group(1).decode("utf-8").replace("\\", "/")
            local = original.parent / include
            if local.is_file():
                self.add(local)
                continue
            for root in self.roots:
                found = next((root / directory / include for directory in NATIVE_INCLUDE_DIRS
                              if (root / directory / include).is_file()), None)
                if found:
                    self.add(found)
                    break
        return original


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="reusable workspace output directory")
    parser.add_argument("--source-root", type=Path, default=REPOSITORY, help="project source tree to compile directly")
    parser.add_argument("--dependency-source-root", type=Path, help="matching dependency-consumer source tree")
    parser.add_argument("--loader-source", type=Path, help="explicit original/deployed l_studio.cpp")
    parser.add_argument("--guard-source", type=Path, help="explicit original/deployed csol_sequence_guard.cpp")
    parser.add_argument("--storage-header", type=Path, help="candidate shared storage planner header")
    parser.add_argument("--dependency-loader", type=Path, help="explicit deployed csol_loader.cpp")
    parser.add_argument("--sv-main", type=Path, help="explicit deployed sv_main.cpp")
    parser.add_argument("--sdk-root", type=Path, default=REPOSITORY / "external/ReHLDS/rehlds")
    parser.add_argument("--rapidjson-include", type=Path,
                        default=REPOSITORY / "external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include")
    parser.add_argument("--compiler", default="clang-cl.exe")
    parser.add_argument("--environment-json", type=Path, help="optional private environment dictionary; values are not logged")
    parser.add_argument("--loader-only", action="store_true", help="omit download/dependency tests")
    parser.add_argument("--expect-legacy", action="store_true", help="require the old allocation assertion to fail")
    parser.add_argument("--expect-double-plan", action="store_true", help="require the old duplicate-plan assertion to fail")
    parser.add_argument("--prepare-only", action="store_true", help="write include adapters and commands without compiling")
    args = parser.parse_args()
    if args.expect_legacy and args.expect_double_plan:
        parser.error("choose only one expected regression")

    output = args.out.resolve()
    if not output.is_relative_to(REPOSITORY / "build"):
        parser.error("--out must stay under the workspace build directory")
    source = args.source_root.resolve(strict=True)
    dependency_source = (args.dependency_source_root or source).resolve(strict=True)
    sdk = args.sdk_root.resolve(strict=True)
    rapidjson = args.rapidjson_include.resolve(strict=True)
    compiler_name = shutil.which(args.compiler) or args.compiler
    compiler = Path(compiler_name).resolve(strict=True)
    if not (sdk / "rehlds/precompiled.h").is_file():
        parser.error("--sdk-root must contain rehlds/precompiled.h")
    if not (rapidjson / "rapidjson/document.h").is_file():
        parser.error("--rapidjson-include must contain rapidjson/document.h")
    output.mkdir(parents=True, exist_ok=True)
    inputs = Inputs([source, dependency_source])
    selected = {
        "loader": (args.loader_source or source / "external/ReHLDS/rehlds/engine/l_studio.cpp",
                   "external/ReHLDS/rehlds/engine/l_studio.cpp"),
        "guard": (args.guard_source or source / "native/engine/csol_sequence_guard.cpp",
                  "native/engine/csol_sequence_guard.cpp"),
        "sequences": (source / "native/csol/sequences.cpp", "native/csol/sequences.cpp"),
    }
    storage = inputs.add(args.storage_header or source / "native/engine/csol_studio_storage.h")
    (output / "storage-under-test.inc").write_text(f'#include "{storage.as_posix()}"\n', encoding="utf-8")
    inputs.add(source / "native/csol/sequence_adapter_fixture.hpp", "native/csol/sequence_adapter_fixture.hpp")
    for label, (original, relative) in selected.items():
        selected_path = inputs.add(original, relative)
        (output / f"{label}-under-test.inc").write_text(f'#include "{selected_path.as_posix()}"\n', encoding="utf-8")
    fixture = inputs.add(HERE / "fixture.cpp")
    inputs.add(Path(__file__))
    extra_sources = []
    extractions = []
    if not args.loader_only:
        original = args.dependency_loader or dependency_source / "native/engine/csol_loader.cpp"
        selected_path = inputs.add(original, "native/engine/csol_loader.cpp")
        data = selected_path.read_bytes()
        boundary = b"\nvoid GoldCraftCsolSequenceDelta("
        if data.count(boundary) != 1:
            raise ValueError("dependency loader preamble changed; review extraction before adapting")
        prefix_end = data.index(boundary)
        start, end = function_span(data, "GoldCraftPrecacheCsolResources")
        extracted = output / "dependency-extract.inc"
        extracted.write_bytes(data[:prefix_end] + b"\n" + data[start:end])
        (output / "dependency-loader-under-test.inc").write_text(
            f'#include "{extracted.as_posix()}"\n', encoding="utf-8")
        extractions.append({"source": identity(original), "output": identity(extracted),
                            "ranges": [[0, prefix_end], [start, end]]})
        svmain = args.sv_main or dependency_source / "external/ReHLDS/rehlds/engine/sv_main.cpp"
        inputs.add(svmain)
        data = svmain.read_bytes()
        snippets, ranges = [], []
        for name in ("PrecacheModelTexture", "PrecacheModelSeqGroups", "PrecacheModelSounds"):
            start, end = function_span(data, name)
            snippets.append(data[start:end])
            ranges.append({"name": name, "startByte": start, "endByte": end,
                           "sha256": hashlib.sha256(data[start:end]).hexdigest()})
        extracted = output / "advertisers-under-test.inc"
        extracted.write_bytes(b"\n".join(snippets))
        extractions.append({"source": identity(svmain), "output": identity(extracted), "ranges": ranges})
        for name in DEPENDENCY_UNITS:
            relative = f"native/csol/{name}.cpp"
            extra_sources.append(inputs.add(dependency_source / relative, relative))

    defines = ["REHLDS_API", "REHLDS_FIXES", "REHLDS_SSE", "REHLDS_OPT_PEDANTIC", "REHLDS_SELF",
               "REHLDS_CHECKS", "HAVE_OPT_STRTOOLS", "USE_BREAKPAD_HANDLER", "DEDICATED", "SWDS",
               "NDEBUG", "NOMINMAX", "_CRT_SECURE_NO_WARNINGS"]
    if not args.loader_only:
        defines.append("CHECK_DEPENDENCIES")
    includes = [output, storage.parent, *(source / d for d in NATIVE_INCLUDE_DIRS)]
    includes += [sdk / d for d in ("", "metamod/include", "public/rehlds", "common", "engine",
                                   "public", "pm_shared", "rehlds", "testsuite")]
    includes += [sdk.parent / "dep/bzip2/include", sdk.parent / "dep/cppunitlite/include", rapidjson]
    flags = ["--target=i686-pc-windows-msvc", "/nologo", "/std:c++20", "/EHsc", "/MT", "/utf-8",
             "/O2", "/clang:-O3", "/clang:-flto=thin", "/arch:AVX2", "/Gy", "/Gw", "/fp:precise",
             "/Y-", "/showIncludes", *[f"/D{d}" for d in defines], *[f"/I{p}" for p in includes]]
    commands, objects = [], []
    for index, unit in enumerate([fixture, *extra_sources]):
        obj = output / f"unit-{index}.obj"
        objects.append(str(obj))
        commands.append({"name": f"compile-{index}", "argv": [str(compiler), *flags, "/c", str(unit), f"/Fo{obj}"]})
    executable = output / "test.exe"
    commands.append({"name": "link", "argv": [str(compiler), "--target=i686-pc-windows-msvc", "-fuse-ld=lld", "/MT",
                                               *objects, f"/Fe{executable}", "/link", "/OPT:REF", "/OPT:ICF",
                                               "/OPT:LLDLTO=3", "/threads:1"]})
    commands.append({"name": "test", "argv": [str(executable)]})
    # Register SDK headers before compilation, then verify the actual consumed
    # inputs as well as the recursive project-source closure after the run.
    for directory in (sdk, rapidjson):
        for path in directory.rglob("*"):
            if path.is_file() and path.suffix.lower() in (".h", ".hpp", ".inl", ".inc"):
                inputs.records[str(path.resolve())] = identity(path)
    generated_inputs = [identity(p) for p in output.glob("*.inc")]
    tracked_inputs = [*inputs.records.values(), *generated_inputs, identity(compiler)]
    plan = {"commands": commands, "sources": list(inputs.records.values()), "extractions": extractions,
            "generatedInputs": generated_inputs, "compiler": identity(compiler), "prepareOnly": args.prepare_only,
            "expectedLegacyFailure": args.expect_legacy, "expectedDoublePlanFailure": args.expect_double_plan,
            "singleCompileJob": True, "liveGame": False}
    if args.environment_json:
        plan["environmentInput"] = identity(args.environment_json)
        tracked_inputs.append(plan["environmentInput"])
    write_json(output / "plan.json", plan)
    if args.prepare_only:
        print(f"Prepared {output / 'plan.json'}; no compilation or execution.")
        return 0
    environment = dict(os.environ)
    if args.environment_json:
        overrides = json.loads(args.environment_json.read_text(encoding="utf-8-sig"))
        if not isinstance(overrides, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in overrides.items()):
            raise ValueError("environment JSON must be a string-to-string dictionary")
        environment.update(overrides)
    environment["VSLANG"] = "1033"
    result = {"success": False, "results": [], "includes": [], "liveGame": False}
    began = time.monotonic()
    try:
        for command in commands:
            started = time.monotonic()
            run = subprocess.run(command["argv"], cwd=output, env=environment, capture_output=True,
                                 text=True, timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
            log = output / f"{command['name']}.log"
            log.write_text(run.stdout + run.stderr, encoding="utf-8")
            result["results"].append({"name": command["name"], "exit": run.returncode,
                                      "seconds": time.monotonic() - started, "log": identity(log)})
            expected = 1 if command["name"] == "test" and (args.expect_legacy or args.expect_double_plan) else 0
            if run.returncode != expected:
                raise RuntimeError(f"{command['name']} exited {run.returncode}; see {log}")
        log = (output / "test.log").read_text(encoding="utf-8")
        if args.expect_legacy and "dedicated allocation must omit texture payload" not in log:
            raise RuntimeError("old version failed for a different reason")
        if args.expect_double_plan and "unhooked load must plan exactly once" not in log:
            raise RuntimeError("old version failed for a different reason")
        data = executable.read_bytes()
        offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[offset:offset + 4] != b"PE\0\0" or struct.unpack_from("<H", data, offset + 4)[0] != 0x14C:
            raise RuntimeError("test executable is not an x86 PE")
        result["executable"] = identity(executable)
        result["success"] = True
    except Exception as error:
        result["error"] = str(error)
    finally:
        result["inputsUnchanged"] = all(identity(Path(row["path"])) == row for row in tracked_inputs)
        result["success"] &= result["inputsUnchanged"]
        headers = set()
        for log in output.glob("compile-*.log"):
            for line in log.read_text(encoding="utf-8").splitlines():
                if "including file:" in line:
                    headers.add(Path(line.split("including file:", 1)[1].strip()).resolve())
        result["includes"] = [identity(path) for path in sorted(headers)]
        result["seconds"] = time.monotonic() - began
        write_json(output / "result.json", result)
    print(json.dumps({k: result[k] for k in ("success", "inputsUnchanged", "seconds")}))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
