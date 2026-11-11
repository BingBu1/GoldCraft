#!/usr/bin/env python3
"""Compile the real presentation TU in place; run lifecycle and x86 oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ("native/guest/cof/aim_fov.hpp", "native/guest/cof/aim_fov.cpp",
          "native/guest/cof/authority.hpp", "native/guest/cof/sound_protocol.hpp",
          "native/guest/cof/client/presentation.hpp", "native/guest/cof/client/presentation.cpp",
          "tests/cryoffear-client-presentation/main.cpp",
          "tests/cryoffear-client-presentation/differential_runner.cpp",
          "tests/cryoffear-client-presentation/run.py", "tests/cryoffear-aim/oracle.py")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", required=True, type=Path)
    parser.add_argument("--environment", required=True, type=Path)
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
    flags = ["/nologo", "/c", "/TP", "/W4", "/WX", "/utf-8", "/EHsc", "/MT", "/std:c++20",
             "--target=i686-pc-windows-msvc", "/arch:AVX2", "/O2", "/clang:-O3",
             "/clang:-flto=thin", "/fp:precise", "/DNDEBUG"]
    commands = [[str(compiler), *flags, "/Fo" + str(out) + os.sep,
                 *[str(ROOT / p) for p in INPUTS if p.endswith(".cpp")]]]
    for label, unit in (("lifecycle", "main"), ("differential", "differential_runner")):
        commands.append([str(linker), "/nologo", "/machine:X86", "/subsystem:console", "/incremental:no",
                         "/OPT:REF", "/OPT:ICF", "/out:" + str(out / (label + ".exe")),
                         *[str(out / (name + ".obj")) for name in ("aim_fov", "presentation", unit)]])
    logs = []
    for argv in commands:
        result = subprocess.run(argv, cwd=ROOT, env=environment, capture_output=True, text=True)
        logs.append({"program": Path(argv[0]).name, "exitCode": result.returncode,
                     "stdout": result.stdout, "stderr": result.stderr})
        (out / "build-log.json").write_text(json.dumps(logs, indent=2), encoding="utf-8")
        result.check_returncode()
    result = subprocess.run([str(out / "lifecycle.exe")], capture_output=True, text=True, check=True)
    (out / "lifecycle.txt").write_text(result.stdout, encoding="utf-8")
    print(result.stdout.strip())
    subprocess.run([sys.executable, "-I", str(ROOT / "tests/cryoffear-aim/oracle.py"),
                    "--sample", str(args.sample.resolve()), "--runner", str(out / "differential.exe"),
                    "--output", str(out / "fov-differential.json")], cwd=ROOT, check=True)
    outputs = {}
    for name in ("lifecycle.exe", "differential.exe"):
        data = (out / name).read_bytes()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        assert struct.unpack_from("<H", data, pe + 4)[0] == 0x14C
        outputs[name] = hashlib.sha256(data).hexdigest()
    after = {p: sha(ROOT / p) for p in INPUTS}
    assert before == after
    receipt = dict(revision=revision, sourcesBefore=before, sourcesAfter=after,
                   compilerVersion=config["version"].splitlines()[0], compilerSha256=sha(compiler),
                   linkerSha256=sha(linker), flags=flags, architecture="PE32 x86",
                   outputs=outputs, sourceSnapshots=False, gameplay=False, performance=False)
    (out / "compile-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
