#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a serial smoke regression plan and input manifest; never compile."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import os
import shutil
import sys


def identity(path):
    path = path.resolve(strict=True)
    content = path.read_bytes()
    return {"path": str(path), "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}


def guarded_output(path, workspace):
    path = path.resolve()
    if not path.is_relative_to(workspace / "build"):
        raise ValueError("Output must remain under workspace build")
    for node in (path, *path.parents):
        if node.is_symlink() or (hasattr(node, "is_junction") and node.is_junction()):
            raise ValueError("Linked output path refused: " + str(node))
    if path.exists():
        raise FileExistsError("Prior evidence must remain unchanged: " + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--prepared", required=True, type=Path)
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--old-client", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    workspace = args.workspace.resolve(strict=True)
    prepared = args.prepared.resolve(strict=True)
    out = guarded_output(args.output, workspace)
    if not prepared.is_relative_to(workspace / "build"):
        raise ValueError("Prepared source must be a workspace build snapshot")
    candidate = json.loads((prepared / "prepared.json").read_text(encoding="utf-8-sig"))
    for item in candidate["inputs"]:
        if identity(workspace / item["path"])["sha256"] != item["sha256"]:
            raise ValueError("Production source drift: " + item["path"])
    for item in candidate["generated"]:
        actual = identity(prepared / item["path"])
        if actual["sha256"] != item["sha256"] or actual["bytes"] != item["size"]:
            raise ValueError("Prepared source drift: " + item["path"])
    config = json.loads(args.environment.read_text(encoding="utf-8-sig"))
    compiler = Path(config["compilerRoot"]) / "bin/clang-cl.exe"
    linker = compiler.with_name("lld-link.exe")
    old = args.old_client.resolve(strict=True)
    positive = prepared / "source/native/csol/client/scenario/core/scenario_smoke.cpp"
    old_text = old.read_text(encoding="utf-8-sig")
    positive_text = positive.read_text(encoding="utf-8-sig")
    old_hook = 'engine.pfnHookEvent("events/createsmoke.sc", CreateSmoke)'
    new_hook = "engine.pfnHookEvent(smoke::EventPath, CreateSmoke)"
    if old_text.count(old_hook) != 1 or old_text.replace(old_hook, new_hook) != positive_text:
        raise ValueError("Negative control must differ only in the original event registration")
    out.mkdir(parents=True)
    records = {}
    copies = []

    def remember(path, kind):
        record = identity(path)
        record["kind"] = kind
        key = record["path"].casefold()
        previous = records.get(key)
        if previous and previous["sha256"] != record["sha256"]:
            raise ValueError("Input changed during preparation: " + record["path"])
        records[key] = record
        return record

    def independent_copy(source, target):
        before = remember(source, "source-origin")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        after = remember(target, "independent-source-copy")
        if before["sha256"] != after["sha256"] or target.stat().st_nlink != 1 or (
            target.stat().st_dev == source.stat().st_dev and target.stat().st_ino == source.stat().st_ino
        ):
            raise ValueError("Independent copy verification failed: " + str(target))
        copies.append({"source": before, "copy": after, "independent": True})

    for item in candidate["generated"]:
        remember(prepared / item["path"], "prepared-input")
    remember(prepared / "prepared.json", "candidate-manifest")
    remember(args.environment, "compiler-environment")
    # The negative includes the exact old TU and only its four neighboring
    # headers, independently copied. Common particle code is linked once.
    negative = out / "negative-source"
    independent_copy(old, negative / "native/csol/client/scenario/core/scenario_smoke.cpp")
    for relative in ("native/csol/client/runtime.hpp", "native/csol/client/scenario/core/scenario_smoke.hpp", "native/csol/scenario/core/scenario_smoke.hpp"):
        independent_copy(prepared / "source" / relative, negative / relative)
    for name in ("plan.py", "run.py"):
        independent_copy(Path(__file__).resolve().parent / name, out / name)
    (out / "negative-only-change.diff").write_text("".join(difflib.unified_diff(
        old_text.splitlines(True), positive_text.splitlines(True), fromfile="old-routing", tofile="candidate-routing")), encoding="utf-8")

    meta = workspace / "external/MetaHookSv-20261007"
    sdk = meta / "MetaHook/include"
    client_dirs = [sdk / name for name in ("", "Interface", "HLSDK/cl_dll", "HLSDK/common", "HLSDK/engine",
                  "HLSDK/pm_shared", "HLSDK/public", "SourceSDK", "SourceSDK/tier0", "SourceSDK/tier1")]
    client_dirs.append(meta / "thirdparty/glew_fork/include")
    # These read-only SDKs are inventoried in place, not copied as an unrelated
    # tree. Actual compiler includes must belong to this hashed input universe.
    include_dirs = [Path(value) for value in config["INCLUDE"].split(";") if value]
    include_dirs.extend((compiler.parent.parent / "lib/clang").glob("*/include"))
    sdk_roots = [sdk, meta / "thirdparty/glew_fork/include", *include_dirs]
    visited = set()
    for directory in sdk_roots:
        if not directory.is_dir():
            raise FileNotFoundError("Include directory is absent: " + str(directory))
        for path in directory.rglob("*"):
            if path.is_file():
                key = str(path.resolve()).casefold()
                if key not in visited:
                    visited.add(key)
                    remember(path, "sdk-include-universe")
    # Match the /MT C++/CRT library closure. Link /verbose verifies the actual
    # archive paths; any additional archive is an unprepared dependency failure.
    libraries = ("libcmt.lib", "libcpmt.lib", "libvcruntime.lib", "libucrt.lib", "oldnames.lib", "kernel32.lib", "uuid.lib")
    for name in libraries:
        matches = [Path(directory) / name for directory in config["LIB"].split(";") if directory and (Path(directory) / name).is_file()]
        if not matches:
            raise FileNotFoundError("Required x86 library is absent: " + name)
        remember(matches[0], "link-library")
    for tool in (compiler, linker, Path(sys.executable)):
        remember(tool, "tool")
    for dll in compiler.parent.glob("*.dll"):
        remember(dll, "compiler-adjacent-runtime")
    for dll in Path(sys.executable).parent.glob("python*.dll"):
        remember(dll, "python-runtime")
    env = {key: config[key] for key in ("INCLUDE", "LIB", "LIBPATH")}
    env["VSLANG"] = "1033"
    env["CL"] = ""
    env["_CL_"] = ""
    env["LINK"] = ""
    env["_LINK_"] = ""
    flags = ["--target=i686-pc-windows-msvc", "/nologo", "/std:c++20", "/EHsc", "/MT", "/utf-8",
             "/clang:-O3", "/clang:-flto=thin", "/arch:AVX2", "/fp:precise", "/clang:-ffp-contract=off",
             "/Gy", "/Gw", "/W3", "/DNDEBUG", "/DWIN32", "/DNOMINMAX", "/D_CRT_SECURE_NO_WARNINGS"]
    client_flags = ["/DNO_MALLOC_OVERRIDE", "/DGLEW_STATIC", "/DGLEW_NO_GLU"]
    commands = []
    objects = {}

    def compile_one(name, source, includes, extra=()):
        obj = out / (name + ".obj")
        objects[name] = obj
        commands.append({"name": name + "-compile", "kind": "compile", "expectedExit": 0,
                         "command": [str(compiler), *flags, *extra, *["/I" + str(path) for path in includes],
                                     "/showIncludes", "/c", str(source), "/Fo" + str(obj)]})
        return obj

    core = compile_one("smoke-core", prepared / "source/native/csol/scenario/core/scenario_smoke.cpp", [prepared / "source"])
    for name, includes, sources, expected_exit, expected_failures in (
        ("event-order", [prepared, prepared / "source"], [prepared / "event_order.cpp"], 0, []),
        ("client-routing", [prepared / "source", *client_dirs], [prepared / "client_routing.cpp"], 0, []),
        ("old-routing", [negative, prepared / "source", *client_dirs], [prepared / "client_routing.cpp"], 1,
         ["FAIL HUD initialization preserves stock registration", "FAIL scenario registers only its dedicated event name",
          "FAIL stock detonation 1 and continuing puff 4 both reach native callback",
          "FAIL native smoke neither emits nor rejects a scenario cloud"]),
    ):
        obj = compile_one(name, sources[0], includes, client_flags if name != "event-order" else ())
        exe = out / (name + ".exe")
        linked = [obj] if name == "event-order" else [obj, core]
        commands.append({"name": name + "-link", "kind": "link", "expectedExit": 0,
                         "command": [str(compiler), "--target=i686-pc-windows-msvc", "/MT", "/clang:-fuse-ld=lld",
                                     *map(str, linked), "/Fe" + str(exe), "/link", "/MACHINE:X86", "/OPT:REF",
                                     "/OPT:ICF", "/OPT:LLDLTO=3", "/threads:1", "/verbose"]})
        commands.append({"name": name + "-run", "kind": "test", "expectedExit": expected_exit,
                         "expectedFailureLines": expected_failures, "command": [str(exe)]})
    payload = {
        "workspace": str(workspace), "prepared": str(prepared), "maxJobs": 1,
        "compiled": False, "executedTests": False, "inputSourcesUnchanged": len(candidate["inputs"]),
        "environment": env, "files": list(records.values()), "independentCopies": copies,
        "commands": commands,
        "limits": ["Do not execute until the parent explicitly releases the shared compiler window.",
                   "Only serial x86 fixtures; no UI, runtime changes, deployment or Git index writes.",
                   "Do not rerun unchanged 1248 particle/render or 63 USP checks.",
                   "No hard links for source, snapshots, config, cache, logs, DLL/PDB or outputs; all new source copies are independent.",
                   "Protected E:/Steam installation must never be a source or destination.",
                   "Actual includes and linker libraries must match the prehashed SDK/tool/dependency inputs."],
    }
    (out / "plan.json").write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"prepared": True, "compiled": False, "commands": len(commands), "maxJobs": 1,
                      "inputs": len(records), "output": str(out)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
