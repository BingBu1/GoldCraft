#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the real lightstyle service and complete native PF writer body."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    return dict(path=str(path.resolve()), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("compiler", "environment", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / "build"):
        raise ValueError("Use a fresh workspace build directory")
    out.mkdir(parents=True)
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text("utf-8-sig")).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    env["VSLANG"] = "1033"
    source = ROOT / "external/ReHLDS/rehlds/engine/pr_cmds.cpp"
    paths = [Path(__file__), args.compiler, args.environment, source,
             ROOT / "native/engine/lightstyles.cpp", ROOT / "native/engine/lightstyles.h",
             ROOT / "native/include/goldcraft/lightstyle_owner.hpp",
             ROOT / "native/include/goldcraft/lightstyle_api.hpp",
             ROOT / "external/ReHLDS/rehlds/public/interface.h",
             Path(__file__).with_name("main.cpp"),
             Path(__file__).parent / "fixture/precompiled.h"]
    before = [identity(path) for path in paths]
    text = source.read_text("utf-8")
    start = text.index("void EXT_FUNC PF_lightstyle_I(int style, const char *val)")
    end = text.index("\nint EXT_FUNC PF_checkbottom_I", start)
    body = text[start:end]
    (out / "lightstyle_actual.inc").write_text(body, "utf-8")
    steps = []
    success = False

    def run(name, command):
        done = subprocess.run(list(map(str, command)), cwd=out, env=env,
                              capture_output=True, timeout=120)
        log = (done.stdout + done.stderr).decode("utf-8", "replace")
        (out / (name + ".log")).write_text(log, "utf-8")
        steps.append(dict(name=name, command=list(map(str, command)), exit=done.returncode))
        print(name, done.returncode, flush=True)
        if done.returncode:
            print(log[-5000:])
            raise RuntimeError(name)
        if name == "test":
            print(log, flush=True)

    try:
        flags = ["/nologo", "--target=i686-pc-windows-msvc", "/std:c++20", "/EHsc", "/MT",
                 "/DNOMINMAX", "/DREHLDS_FIXES", "/arch:AVX2", "/fp:precise", "/utf-8",
                 "/clang:-O3", "/clang:-flto=thin",
                 "/I" + str(Path(__file__).parent / "fixture"),
                 "/I" + str(ROOT / "native/engine"), "/I" + str(ROOT / "native/include"),
                 "/I" + str(ROOT / "external/ReHLDS/rehlds/public"), "/I" + str(out)]
        objects = []
        for name, file in [("service", ROOT / "native/engine/lightstyles.cpp"),
                           ("fixture", Path(__file__).with_name("main.cpp"))]:
            obj = out / (name + ".obj")
            run(name, [args.compiler, *flags, "/c", file, "/Fo" + str(obj)])
            objects.append(obj)
        exe = out / "lightstyles.exe"
        run("link", [args.compiler, "/nologo", "--target=i686-pc-windows-msvc", "-fuse-ld=lld",
                     "/MT", *objects, "/Fe" + str(exe), "/link", "/machine:x86", "/threads:1"])
        run("test", [exe])
        success = True
    finally:
        stable = before == [identity(path) for path in paths]
        (out / "receipt.json").write_text(json.dumps(dict(success=success and stable,
            inputsUnchanged=stable, inputs=before, steps=steps,
            extracted=dict(bytes=len(body.encode()), sha256=hashlib.sha256(body.encode()).hexdigest()),
            sourceCopies=0, deployed=False, gameplay=False, performance=False,
            boundary="Actual service/registration and complete PF writer, explicit engine globals/network sinks; not a full engine or visual acceptance"), indent=2) + "\n", "utf-8")
    if not success or not stable:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
