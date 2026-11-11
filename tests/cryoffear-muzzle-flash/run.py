#!/usr/bin/env python3
"""Build real SDK event consumer in place and retain input/tool hashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ("native/guest/cof/client/muzzle_flash.hpp", "native/guest/cof/client/muzzle_flash.cpp",
          "tests/cryoffear-muzzle-flash/main.cpp", "tests/cryoffear-muzzle-flash/run.py",
          "tests/cryoffear-muzzle-flash/oracle.py", "tests/cryoffear-aim/oracle.py")


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
    environment = os.environ.copy()
    loaded = json.loads(args.environment.read_text(encoding="utf-8-sig"))
    environment.update({k: v for k, v in loaded.items() if k in ("INCLUDE", "LIB", "LIBPATH", "PATH")})
    before = {p: sha(ROOT / p) for p in INPUTS}
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    sdk = ROOT / "external/MetaHookSv-20261007/MetaHook/include"
    includes = [sdk, sdk / "Interface", *[sdk / "HLSDK" / p for p in
                ("cl_dll", "common", "engine", "pm_shared", "public")], sdk / "SourceSDK",
                sdk / "SourceSDK/tier0", sdk / "SourceSDK/tier1"]
    flags = ["/nologo", "/c", "/TP", "/W4", "/WX", "/utf-8", "/EHsc", "/MT", "/std:c++20",
             "--target=i686-pc-windows-msvc", "/arch:AVX2", "/O2", "/clang:-O3", "/clang:-flto=thin",
             "/fp:precise", "/DNDEBUG", "/DNOMINMAX", "/DNO_MALLOC_OVERRIDE", "/D_CRT_SECURE_NO_WARNINGS"]
    commands = [[str(compiler), *flags, *["/imsvc" + str(p) for p in includes],
                 "/Fo" + str(out) + os.sep, *[str(ROOT / p) for p in INPUTS if p.endswith(".cpp")]],
                [str(linker), "/nologo", "/machine:X86", "/subsystem:console", "/incremental:no",
                 "/OPT:REF", "/OPT:ICF", "/out:" + str(out / "muzzle-flash.exe"),
                 str(out / "muzzle_flash.obj"), str(out / "main.obj")]]
    logs = []
    for argv in commands:
        result = subprocess.run(argv, cwd=ROOT, env=environment, capture_output=True, text=True)
        logs.append(dict(program=Path(argv[0]).name, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (out / "build-log.json").write_text(json.dumps(logs, indent=2), encoding="utf-8")
        if result.returncode:
            print(result.stdout, result.stderr)
        result.check_returncode()
    subprocess.run([sys.executable, "-I", str(ROOT / "tests/cryoffear-muzzle-flash/oracle.py"),
                    "--sample", str(args.sample.resolve()), "--runner", str(out / "muzzle-flash.exe"),
                    "--output", str(out / "differential.json")], check=True)
    data = (out / "muzzle-flash.exe").read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert struct.unpack_from("<H", data, pe + 4)[0] == 0x14C
    after = {p: sha(ROOT / p) for p in INPUTS}
    assert before == after
    sdk_files = {str(p.relative_to(ROOT)): sha(p) for p in sdk.rglob("*") if p.is_file() and
                 p.name in ("metahook.h", "cl_entity.h", "r_efx.h", "studio_event.h", "event_api.h", "APIProxy.h", "event_args.h", "ref_params.h", "com_model.h", "triangleapi.h")}
    receipt = dict(revision=revision, sourcesBefore=before, sourcesAfter=after, sdkInputs=sdk_files,
                   compilerVersion=config["version"].splitlines()[0], compilerSha256=sha(compiler),
                   linkerSha256=sha(linker), flags=flags, architecture="PE32 x86", executableSha256=sha(out / "muzzle-flash.exe"),
                   sourceSnapshots=False, gameplay=False, performance=False)
    (out / "compile-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
