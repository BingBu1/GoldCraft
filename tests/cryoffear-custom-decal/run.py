#!/usr/bin/env python3
"""Narrow x86 build; no game DLL, deployment, or source snapshot."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ["native/guest/cof/client/custom_decal.hpp", "native/guest/cof/client/custom_decal.cpp",
          "native/csol/client/weapons/guest_cof_decals.hpp", "native/csol/client/weapons/guest_cof_decals.cpp",
          "native/guest/cof/resource_catalog.hpp", "tests/cryoffear-custom-decal/adapter.cpp",
          "native/csol/client/weapons/guest_cof_resources.cpp", "native/csol/client/weapons/guest_cof_resources.hpp",
          "tests/cryoffear-custom-decal/resource_owner.cpp",
          "tests/cryoffear-custom-decal/main.cpp", "tests/cryoffear-custom-decal/run.py",
          "tests/cryoffear-custom-decal/oracle.py"]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--sample", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / "build")
    out.mkdir(parents=True, exist_ok=True)
    config = json.loads((ROOT / ".tools/clang-toolchain.json").read_text(encoding="utf-8-sig"))
    compiler = Path(config["root"]) / "bin/clang-cl.exe"
    linker = compiler.with_name("lld-link.exe")
    env = os.environ.copy()
    env.update({k: v for k, v in json.loads(args.environment.read_text(encoding="utf-8-sig")).items()
                if k in ("INCLUDE", "LIB", "LIBPATH", "PATH")})
    before = {p: sha(ROOT / p) for p in INPUTS}
    (out / "inputs-before.json").write_text(json.dumps(before, indent=2))
    sdk = ROOT / "external/MetaHookSv-20261007/MetaHook/include"
    includes = [ROOT / "external/MetaHookSv-20261007/thirdparty/glew_fork/include", sdk, sdk / "Interface", *[sdk / "HLSDK" / p for p in
                ("cl_dll", "common", "engine", "pm_shared", "public")], sdk / "SourceSDK",
                sdk / "SourceSDK/tier0", sdk / "SourceSDK/tier1"]
    flags = ["/nologo", "/c", "/TP", "/W4", "/WX", "/utf-8", "/EHsc", "/MT", "/std:c++20",
             "--target=i686-pc-windows-msvc", "/arch:AVX2", "/O2", "/clang:-O3", "/clang:-flto=thin",
             "/fp:precise", "/clang:-ffp-contract=off", "-Wno-overriding-option", "/DNDEBUG", "/DNOMINMAX", "/DNO_MALLOC_OVERRIDE", "/D_CRT_SECURE_NO_WARNINGS"]
    commands = [[str(compiler), *flags, *["/imsvc" + str(p) for p in includes],
                 "/Fo" + str(out) + os.sep, str(ROOT / p)] for p in INPUTS if p.endswith(".cpp")]
    commands.append([str(linker), "/nologo", "/machine:X86", "/subsystem:console", "/incremental:no",
                     "/OPT:REF", "/OPT:ICF", "/out:" + str(out / "decals.exe"),
                     str(out / "custom_decal.obj"), str(out / "guest_cof_decals.obj"), str(out / "guest_cof_resources.obj"), str(out / "adapter.obj"), str(out / "resource_owner.obj"), str(out / "main.obj"), "opengl32.lib", "gdi32.lib", "user32.lib"])
    logs = []
    for argv in commands:
        result = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, text=True)
        logs.append(dict(argv=argv, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (out / "build-log.json").write_text(json.dumps(logs, indent=2))
        if result.returncode:
            print(result.stdout, result.stderr)
        result.check_returncode()
    result = subprocess.run([str(out / "decals.exe")], capture_output=True, text=True)
    (out / "lifecycle-gl.txt").write_text(result.stdout + result.stderr)
    print(result.stdout, result.stderr)
    result.check_returncode()
    subprocess.run([sys.executable, "-I", str(ROOT / INPUTS[-1]), "--sample", str(args.sample.resolve()),
                    "--runner", str(out / "decals.exe"), "--output", str(out / "differential.json")], check=True)
    after = {p: sha(ROOT / p) for p in INPUTS}
    assert before == after
    receipt = dict(revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                   sourcesBefore=before, sourcesAfter=after, compiler=sha(compiler), linker=sha(linker),
                   flags=flags, architecture="PE32 x86", maxJobs=1, executable=sha(out / "decals.exe"),
                   sourceSnapshots=False, gameplay=False, livePerformance=False)
    (out / "compile-receipt.json").write_text(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
