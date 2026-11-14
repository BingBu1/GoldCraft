#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Load a sealed complete engine in an isolated x86 factory test process."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from run import ROOT, identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("compiler", "environment", "engine", "steam-api", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / "build"):
        raise ValueError("Use a fresh workspace build directory")
    for file in (args.engine, args.steam_api):
        if not file.resolve(strict=True).is_relative_to(ROOT):
            raise ValueError("Runtime dependencies must come from workspace copies")
    out.mkdir(parents=True)
    source = Path(__file__).with_name("dll.cpp")
    paths = [Path(__file__), source, args.compiler, args.environment, args.engine, args.steam_api,
             ROOT / "native/include/goldcraft/lightstyle_api.hpp",
             ROOT / "external/ReHLDS/rehlds/public/interface.h"]
    before = [identity(path) for path in paths]
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text("utf-8-sig")).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    temporary = out / "temporary"
    temporary.mkdir()
    env["TMP"] = env["TEMP"] = str(temporary)
    for source_file, name in ((args.engine, "swds.dll"), (args.steam_api, "steam_api.dll")):
        target = out / name
        shutil.copy2(source_file, target)
        if identity(target)["sha256"] != identity(source_file)["sha256"] or target.stat().st_nlink != 1:
            raise ValueError("Independent sealed runtime copy did not verify")
    exe = out / "engine-factory.exe"
    command = [str(args.compiler.resolve()), "/nologo", "--target=i686-pc-windows-msvc",
        "/std:c++20", "/EHsc", "/MT", "/DNOMINMAX", "/arch:AVX2", "/fp:precise",
        "/clang:-O3", "/clang:-flto=thin", "-fuse-ld=lld",
        "/I" + str(ROOT / "native/include"),
        "/I" + str(ROOT / "external/ReHLDS/rehlds/public"),
        str(source), "/Fo" + str(out / "dll.obj"), "/Fe" + str(exe), "/link", "/machine:x86", "/threads:1"]
    steps = []
    success = False
    try:
        for name, argv in [("compile", command), ("load", [str(exe), str(out / "swds.dll")])]:
            done = subprocess.run(argv, cwd=out, env=env, capture_output=True, timeout=120)
            text = (done.stdout + done.stderr).decode("utf-8", "replace")
            (out / (name + ".log")).write_text(text, "utf-8")
            steps.append(dict(name=name, command=argv, exit=done.returncode))
            print(name, done.returncode, text[-3000:], flush=True)
            if done.returncode:
                raise RuntimeError(name)
        success = True
    finally:
        stable = before == [identity(path) for path in paths]
        (out / "receipt.json").write_text(json.dumps(dict(success=success and stable,
            inputsUnchanged=stable, inputs=before, steps=steps,
            engine=identity(out / "swds.dll"), deployed=False, gameplay=False,
            boundary="Real complete engine module factory/ABI in a separate x86 process; no server initialization or visuals"), indent=2) + "\n", "utf-8")
    if not success or not stable:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
