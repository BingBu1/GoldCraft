#!/usr/bin/env python3
"""Compile real guest transport/catalog/presentation sources, without game data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    "native/guest/cof/authority.hpp", "native/guest/cof/catalog.hpp",
    "native/guest/cof/scenario_damage.hpp", "native/guest/cof/aim_fov.hpp",
    "native/guest/cof/aim_fov.cpp", "native/guest/cof/presentation_protocol.hpp",
    "native/guest/cof/presentation_protocol.cpp",
    "native/guest/cof/sound_protocol.hpp", "native/guest/cof/sound_protocol.cpp",
    "native/guest/cof/particle_protocol.hpp", "native/guest/cof/particle_protocol.cpp",
    "native/guest/cof/client/presentation.hpp", "native/guest/cof/client/presentation.cpp",
    "tests/cryoffear-protocol/main.cpp", "tests/cryoffear-protocol/run.py")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / "build") or out == ROOT / "build":
        parser.error("Output must be a dedicated workspace/build directory")
    out.mkdir(parents=True, exist_ok=True)
    config = json.loads((ROOT / ".tools/clang-toolchain.json").read_text(encoding="utf-8-sig"))
    compiler = Path(config["root"]) / "bin/clang-cl.exe"
    environment = os.environ.copy()
    environment.update({k: v for k, v in json.loads(args.environment.read_text(encoding="utf-8-sig")).items()
                        if k in ("INCLUDE", "LIB", "LIBPATH", "PATH")})
    before = {p: sha(ROOT / p) for p in SOURCES}
    flags = ["/nologo", "/c", "/TP", "/W4", "/WX", "/utf-8", "/EHsc", "/MT", "/std:c++20",
             "--target=i686-pc-windows-msvc", "/arch:AVX2", "/O2", "/clang:-O3",
             "/clang:-flto=thin", "/fp:precise", "/DNDEBUG"]
    flags.extend(["/clang:-ffp-contract=off", "-Wno-overriding-option"])
    commands = [[str(compiler), *flags, "/Fo" + str(out) + os.sep,
                 *[str(ROOT / p) for p in SOURCES if p.endswith(".cpp")]]]
    commands.append([str(compiler.with_name("lld-link.exe")), "/nologo", "/machine:X86",
                     "/subsystem:console", "/incremental:no", "/OPT:REF", "/OPT:ICF",
                     "/out:" + str(out / "protocol.exe"),
                     *[str(out / (Path(p).stem + ".obj")) for p in SOURCES if p.endswith(".cpp")]])
    results = []
    for command in [*commands, [str(out / "protocol.exe")]]:
        result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True)
        results.append({"exitCode": result.returncode, "stdout": result.stdout.decode("utf-8", errors="replace"),
                        "stderr": result.stderr.decode("utf-8", errors="replace")})
        (out / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        result.check_returncode()
    after = {p: sha(ROOT / p) for p in SOURCES}
    if before != after:
        raise RuntimeError("Source changed during verification")
    receipt = dict(revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT).decode().strip(),
                   flags=flags, compilerSha256=sha(compiler), before=before, after=after,
                   sourceCopies=False, results=results, gameplay=False, performanceAccepted=False)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(results[-1]["stdout"].strip())


if __name__ == "__main__":
    main()
