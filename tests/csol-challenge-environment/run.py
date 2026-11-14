#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify environment configuration/wire and the actual SDK client consumer."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("client-build", "environment", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--settings-regression", action="store_true",
                        help="Also run the complete existing commented/grouped settings codec suite")
    parser.add_argument("--input-fixture", type=Path,
                        help="Run a matched native input consumer fixture instead of the environment groups")
    args = parser.parse_args()
    if args.input_fixture and (not args.input_fixture.resolve().is_relative_to(ROOT / "tests") or not args.input_fixture.is_file()):
        raise ValueError("Native input fixture must be an existing workspace test")
    spec = importlib.util.spec_from_file_location("client_tools", ROOT / "tests/csol-challenge-hud/run_owner.py")
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / "build"):
        raise ValueError("Use a fresh workspace output")
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text("utf-8-sig")).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    env["VSLANG"] = "1033"
    build = args.client_build.resolve(strict=True)
    cache = (build / "CMakeCache.txt").read_text("utf-8-sig")
    ninja = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$", cache, re.M)[1].strip()
    database = json.loads(subprocess.check_output([ninja, "-C", str(build), "-t", "compdb"], env=env))
    row = next(r for r in database if r["file"].replace("\\", "/").endswith("/client/scenario/core/challenge_phase.cpp"))
    original = helper.split(row["command"])
    flags = []
    skip = False
    for flag in original[1:]:
        if skip:
            skip = False
            continue
        if flag.lower() in ("-o", "-mf", "-mt"):
            skip = True
            continue
        if flag in ("/c", "-c", "-MD", "--") or flag.lower().endswith(".cpp") or flag.lower().startswith(("/fo", "/fd", "-clang:-md", "-clang:-mt", "-clang:-mf")):
            continue
        flags.append(flag)
    flags += ["/showIncludes", "/DCSOL_RESOURCE_REFRESH=1"]
    controls = [Path(__file__), ROOT / "tests/csol-challenge-hud/run_owner.py", args.environment,
                Path(original[0]), build / "build.ninja", build / "CMakeCache.txt"]
    # Capture before compilation, then keep only actual include consumers.
    inventory = {}
    for directory in ("native", "tests", "external/MetaHookSv-20261007/MetaHook/include",
                      "external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include"):
        for path in (ROOT / directory).rglob("*"):
            if path.is_file() and path.suffix.lower() in (".cpp", ".h", ".hpp", ".inc"):
                inventory[str(path.resolve()).casefold()] = helper.identity(path)
    before = [helper.identity(p) for p in controls]
    out.mkdir(parents=True)
    temporary = out / "temporary"
    temporary.mkdir()
    env["TMP"] = env["TEMP"] = str(temporary)
    steps, consumed = [], {}
    success = False

    def run(name, argv):
        done = subprocess.run(list(map(str, argv)), cwd=out, env=env, capture_output=True, timeout=180)
        text = (done.stdout + done.stderr).decode("utf-8", "replace")
        (out / (name + ".log")).write_text(text, "utf-8")
        for included in re.findall(r"Note: including file:\s*([^\r\n]+)", text):
            path = Path(included.strip()).resolve(strict=True)
            prior = inventory.get(str(path).casefold())
            current = helper.identity(path)
            if prior and prior != current:
                raise ValueError("Actual dependency changed during compile: " + str(path))
            consumed[str(path)] = prior or current
        steps.append(dict(name=name, command=list(map(str, argv)), exit=done.returncode))
        print(name, done.returncode, flush=True)
        if done.returncode:
            print(text[-6000:])
            raise RuntimeError(name)
        if name.endswith("test"):
            print(text, flush=True)

    try:
        settings = ROOT / "native/csol/scenario/custom/settings.cpp"
        groups = [("protocol", [Path(__file__).with_name("protocol.cpp"), settings]),
                  ("client", [Path(__file__).with_name("client.cpp")])]
        if args.settings_regression:
            groups.append(("settings", [ROOT / "tests/csol-custom-settings/main.cpp", settings]))
        if args.input_fixture:
            groups = [("native-input", [args.input_fixture.resolve()])]
        compiled = {}
        for group, sources in groups:
            objects = []
            for i, source in enumerate(sources):
                prior = inventory[str(source.resolve()).casefold()]
                consumed[str(source.resolve())] = prior
                obj = compiled.get(source)
                if obj is None:
                    obj = out / f"{group}-{i}.obj"
                    run(f"{group}-compile-{i}", [original[0], *flags, "/c", source, "/Fo" + str(obj)])
                    compiled[source] = obj
                objects.append(obj)
            exe = out / (group + ".exe")
            run(group + "-link", [original[0], "/nologo", "--target=i686-pc-windows-msvc", "-fuse-ld=lld",
                "/MT", *objects, "/Fe" + str(exe), "/link", "/machine:x86", "/threads:1", "/opt:lldltojobs=1"])
            run(group + "-test", [exe])
        success = True
    finally:
        stable = before == [helper.identity(p) for p in controls] and all(helper.identity(Path(p)) == r for p, r in consumed.items())
        (out / "receipt.json").write_text(json.dumps(dict(success=success and stable, inputsUnchanged=stable,
            controls=before, actualDependencies=list(consumed.values()), steps=steps, sourceCopies=0,
            deployed=False, gameplay=False, performance=False,
            boundary=("Actual native input SDK adapter with an explicit simulated downstream command boundary; no original whole-client execution, live PM, FPS or multiplayer evidence"
                      if args.input_fixture else
                      "Real configuration/protocol/fade and actual client SDK hooks with controlled TriangleAPI sinks; no live Renderer or multiplayer evidence")), indent=2) + "\n", "utf-8")
    if not success or not stable:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
