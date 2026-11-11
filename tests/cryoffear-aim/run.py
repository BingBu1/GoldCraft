#!/usr/bin/env python3
"""Build actual sources in place and run the original-instruction differential."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("native/guest/cof/aim_fov.hpp", "native/guest/cof/aim_fov.cpp",
           "tests/cryoffear-aim/runner.cpp", "tests/cryoffear-aim/oracle.py",
           "tests/cryoffear-aim/run.py")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", required=True, type=Path)
    parser.add_argument("--environment", required=True, type=Path,
                        help="existing x86 MSVC INCLUDE/LIB environment JSON")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / "build"), "Compiler outputs must stay in workspace/build"
    out.mkdir(parents=True, exist_ok=True)
    config = json.loads((ROOT / ".tools/clang-toolchain.json").read_text(encoding="utf-8-sig"))
    compiler = Path(config["root"]) / "bin/clang-cl.exe"
    linker = compiler.with_name("lld-link.exe")
    environment = os.environ.copy()
    loaded = json.loads(args.environment.read_text(encoding="utf-8-sig"))
    environment.update({key: value for key, value in loaded.items() if key in ("INCLUDE", "LIB", "LIBPATH", "PATH")})
    before = {p: sha(ROOT / p) for p in SOURCES}
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    flags = ["/nologo", "/c", "/TP", "/W4", "/WX", "/utf-8", "/EHsc", "/MT", "/std:c++20",
             "--target=i686-pc-windows-msvc", "/arch:AVX2", "/O2", "/clang:-O3",
             "/clang:-flto=thin", "/fp:precise", "/DNDEBUG"]
    command = [str(compiler), *flags, "/Fo" + str(out) + os.sep,
               str(ROOT / "native/guest/cof/aim_fov.cpp"), str(ROOT / "tests/cryoffear-aim/runner.cpp")]
    logs = []
    for argv in (command, [str(linker), "/nologo", "/machine:X86", "/subsystem:console",
                          "/incremental:no", "/OPT:REF", "/OPT:ICF", "/out:" + str(out / "aim-fov.exe"),
                          str(out / "aim_fov.obj"), str(out / "runner.obj")]):
        result = subprocess.run(argv, cwd=ROOT, env=environment, capture_output=True, text=True)
        logs.append({"program": Path(argv[0]).name, "exitCode": result.returncode,
                     "stdout": result.stdout, "stderr": result.stderr})
        (out / "build-log.json").write_text(json.dumps(logs, indent=2), encoding="utf-8")
        result.check_returncode()
    executable = (out / "aim-fov.exe").read_bytes()
    pe = struct.unpack_from("<I", executable, 0x3C)[0]
    assert struct.unpack_from("<H", executable, pe + 4)[0] == 0x14C
    after = {p: sha(ROOT / p) for p in SOURCES}
    assert before == after, "Source changed during compilation"
    receipt = dict(revision=revision, sourcesBefore=before, sourcesAfter=after,
                   compilerVersion=config["version"].splitlines()[0], compilerSha256=sha(compiler),
                   linkerSha256=sha(linker), flags=flags, architecture="PE32 x86",
                   executableSha256=hashlib.sha256(executable).hexdigest(), sourceSnapshots=False)
    (out / "compile-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    subprocess.run([sys.executable, "-I", str(ROOT / "tests/cryoffear-aim/oracle.py"),
                    "--sample", str(args.sample.resolve()), "--runner", str(out / "aim-fov.exe"),
                    "--output", str(out / "differential.json")], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
