#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual MetaHook wrapper, plugin/session TUs and exercise wire-to-FOV.

This uses real SDK declarations, not a replacement API header or copied source.
The controlled engine is not gameplay, original visual or performance acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CPP = ("native/csol/client/weapons/guest_cof.cpp", "native/csol/client/weapons/guest_cof_ironsight.cpp", "native/guest/cof/aim_fov.cpp",
       "native/guest/cof/presentation_protocol.cpp", "native/guest/cof/client/presentation.cpp",
       "native/guest/cof/client/revolver_cylinder.cpp",
       "native/csol/client/session.cpp", "native/csol/client/weapons/guest_cof_effects.cpp",
       "native/csol/client/weapons/guest_cof_audio.cpp", "native/guest/cof/client/audio.cpp",
       "native/guest/cof/sound_protocol.cpp", "native/guest/cof/client/weapon_effects.cpp",
       "native/guest/cof/client/view_punch.cpp", "native/guest/cof/client/forward_displacement.cpp",
       "native/csol/client/weapons/guest_cof_mouse.cpp", "native/guest/cof/client/mouse_input.cpp",
       "tests/cryoffear-client-bridge/main.cpp")
INPUTS = (*CPP, "native/csol/client/weapons/guest_cof.hpp", "native/csol/client/runtime.hpp",
          "native/csol/client/plugin.cpp", "native/csol/client/session.cpp", "native/csol/CMakeLists.txt",
          "native/guest/cof/aim_fov.hpp", "native/guest/cof/authority.hpp", "native/guest/cof/catalog.hpp",
          "native/guest/cof/scenario_damage.hpp", "native/guest/cof/presentation_protocol.hpp",
          "native/guest/cof/client/presentation.hpp", "tests/cryoffear-client-bridge/run.py")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--commands", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        parser.error("Output must be a dedicated build subdirectory")
    output.mkdir(parents=True, exist_ok=True)
    config = json.loads((ROOT / ".tools/clang-toolchain.json").read_text(encoding="utf-8-sig"))
    compiler = Path(config["root"]) / "bin/clang-cl.exe"
    linker = compiler.with_name("lld-link.exe")
    env = os.environ.copy()
    env.update({k: v for k, v in json.loads(args.environment.read_text(encoding="utf-8-sig")).items()
                if k in ("INCLUDE", "LIB", "LIBPATH", "PATH")})
    database = json.loads(args.commands.read_text(encoding="utf-8"))
    command = next(e["command"] for e in database
                   if e["file"].replace("\\", "/").endswith("/native/csol/client/plugin.cpp"))
    # SDK paths/definitions are taken from the actual CMake command, not guessed.
    tokens = re.findall(r'"[^"\r\n]*"|[^\s]+', command)
    includes = [Path(p[2:]) for p in tokens if p.startswith("-I")]
    defines = [p for p in tokens if p.startswith("-D")]
    flags = ["/nologo", "/c", "/TP", "/utf-8", "/EHsc", "/MT", "/std:c++20", "/W3", "/WX",
             "--target=i686-pc-windows-msvc", "/arch:AVX2", "/O2", "/clang:-O3", "/clang:-flto=thin",
             "/fp:precise", "/clang:-ffp-contract=off", "-Wno-overriding-option", "/DNDEBUG", *defines,
             *["/imsvc" + str(p) for p in includes]]
    # Follow every real include emitted by Clang, including plugin/session headers.
    before = {p: sha(ROOT / p) for p in INPUTS}
    dependency_paths = set()
    logs = []
    sources = (*CPP, "native/csol/client/plugin.cpp")
    for index, source in enumerate(sources):
        depfile = output / (str(index) + ".d")
        argv = [str(compiler), *[p for p in flags if p != "/c"], "/clang:-M", "/clang:-MF", "/clang:" + str(depfile),
                "/clang:-MT", "/clang:adapter", str(ROOT / source)]
        result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True)
        logs.append(dict(stage="dependencies", source=source, exitCode=result.returncode,
                         stdout=result.stdout, stderr=result.stderr))
        (output / "build-log.json").write_text(json.dumps(logs, indent=2), encoding="utf-8")
        if result.returncode:
            print(result.stdout, result.stderr)
            result.check_returncode()
        make = depfile.read_text(encoding="utf-8").replace("\\\n", " ")
        names = re.findall(r'(?:\\ |[^\s])+', make.split(":", 1)[1])
        dependency_paths.update(Path(name.replace("\\ ", " ")).resolve() for name in names)
    dependencies = {str(p): sha(p) for p in sorted(dependency_paths)}
    for source in sources:
        obj = output / (Path(source).stem + ".obj")
        argv = [str(compiler), *flags, "/Fo" + str(obj), str(ROOT / source)]
        result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True)
        logs.append(dict(source=source, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (output / "build-log.json").write_text(json.dumps(logs, indent=2), encoding="utf-8")
        if result.returncode:
            print(result.stdout, result.stderr)
            result.check_returncode()
    argv = [str(linker), "/nologo", "/machine:X86", "/subsystem:console", "/incremental:no",
            "/OPT:REF", "/OPT:ICF", "/out:" + str(output / "bridge.exe"),
            *[str(output / (Path(p).stem + ".obj")) for p in CPP], "ole32.lib"]
    result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True)
    logs.append(dict(stage="link", exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
    (output / "build-log.json").write_text(json.dumps(logs, indent=2), encoding="utf-8")
    if result.returncode:
        print(result.stdout, result.stderr)
        result.check_returncode()
    result = subprocess.run([str(output / "bridge.exe")], capture_output=True, text=True)
    (output / "result.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
    print(result.stdout.strip())
    result.check_returncode()
    after = {p: sha(ROOT / p) for p in INPUTS}
    stable = all(sha(p) == value for p, value in dependencies.items())
    if before != after or not stable:
        raise RuntimeError("Build inputs changed during verification")
    binary = (output / "bridge.exe").read_bytes()
    pe = struct.unpack_from("<I", binary, 0x3C)[0]
    assert struct.unpack_from("<H", binary, pe + 4)[0] == 0x14C
    receipt = dict(revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                   before=before, after=after, dependencies=dependencies, flags=flags,
                   compilerSha256=sha(compiler), linkerSha256=sha(linker),
                   commandsSha256=sha(args.commands), environmentSha256=sha(args.environment),
                   executableSha256=sha(output / "bridge.exe"), actualSdk=True,
                   pluginAndSessionCompile=True, inputClosureStable=stable,
                   sourceCopies=False, fullDll=False, gameplay=False, performanceAccepted=False)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
