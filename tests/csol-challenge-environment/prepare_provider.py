#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a controlled engine provider from actual lightstyle production code."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("compiler", "environment", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("light_test", ROOT / "tests/csol-environment-lightstyles/run.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / "build"):
        raise ValueError("Use a fresh workspace build output")
    source = ROOT / "external/ReHLDS/rehlds/engine/pr_cmds.cpp"
    text = source.read_text("utf-8")
    start = text.index("void EXT_FUNC PF_lightstyle_I(int style, const char *val)")
    body = text[start:text.index("\nint EXT_FUNC PF_checkbottom_I", start)]
    units = [ROOT / "native/engine/lightstyles.cpp", Path(__file__).with_name("light_provider.cpp")]
    paths = [Path(__file__), args.compiler, args.environment, source, *units,
        ROOT / "native/engine/lightstyles.h", ROOT / "native/include/goldcraft/lightstyle_owner.hpp",
        ROOT / "native/include/goldcraft/lightstyle_api.hpp", ROOT / "external/ReHLDS/rehlds/public/interface.h",
        ROOT / "tests/csol-environment-lightstyles/fixture/precompiled.h"]
    before = [helper.identity(p) for p in paths]
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text("utf-8-sig")).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    out.mkdir(parents=True)
    (out / "lightstyle_actual.inc").write_text(body, "utf-8")
    temporary = out / "temporary"
    temporary.mkdir()
    env["TMP"] = env["TEMP"] = str(temporary)
    steps = []
    success = False
    try:
        objects = []
        for i, unit in enumerate(units):
            obj = out / f"{i}.obj"
            command = [str(args.compiler.resolve()), "/nologo", "--target=i686-pc-windows-msvc",
                "/std:c++20", "/EHsc", "/MT", "/DNOMINMAX", "/DREHLDS_FIXES", "/arch:AVX2",
                "/fp:precise", "/clang:-O3", "/clang:-flto=thin",
                "/I" + str(ROOT / "tests/csol-environment-lightstyles/fixture"),
                "/I" + str(ROOT / "native/engine"), "/I" + str(ROOT / "native/include"),
                "/I" + str(ROOT / "external/ReHLDS/rehlds/public"), "/I" + str(out),
                "/c", str(unit), "/Fo" + str(obj)]
            done = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=120)
            (out / f"compile-{i}.log").write_bytes(done.stdout + done.stderr)
            steps.append(dict(name=f"compile-{i}", command=command, exit=done.returncode))
            if done.returncode:
                raise RuntimeError((done.stdout + done.stderr).decode("utf-8", "replace"))
            objects.append(str(obj))
        dll = out / "swds.dll"
        command = [str(args.compiler.resolve()), "/nologo", "--target=i686-pc-windows-msvc",
            "-fuse-ld=lld", "/LD", "/MT", *objects, "/Fe" + str(dll), "/link", "/machine:x86", "/threads:1"]
        done = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=120)
        (out / "link.log").write_bytes(done.stdout + done.stderr)
        steps.append(dict(name="link", command=command, exit=done.returncode))
        if done.returncode:
            raise RuntimeError((done.stdout + done.stderr).decode("utf-8", "replace"))
        success = True
    finally:
        stable = before == [helper.identity(p) for p in paths]
        receipt = dict(success=success and stable, inputsUnchanged=stable, inputs=before, steps=steps,
            extractedSha256=hashlib.sha256(body.encode()).hexdigest(), sourceCopies=0,
            boundary="Real engine lightstyle service and PF writer over controlled map/global/message boundaries; not a real server")
        if success:
            receipt["artifact"] = helper.identity(dll)
        (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", "utf-8")
    print(json.dumps({k: receipt[k] for k in ("success", "inputsUnchanged", "artifact")}))
    if not success or not stable:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
