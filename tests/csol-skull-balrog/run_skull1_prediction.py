#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the SKULL1 predictor against original recoil data and a pre-fix source.

The baseline supplies the already configured x86 SDK compiler command and
environment. It does not supply reused game objects or assert test success.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def read(path):
    return json.loads(Path(path).read_text("utf-8-sig"))


def write(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), "utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "oracle", "old-source", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / "build") and not out.exists()
    out.mkdir()
    (out / "temp").mkdir()
    baseline = read(args.baseline)
    oracle_meta = read(args.oracle.with_suffix(".json"))
    assert identity(args.oracle)["sha256"] == oracle_meta["sha256"]
    assert oracle_meta["moduleSha256"] == "39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2"
    template = next(job["argv"] for job in baseline["commands"] if job["name"] == "fixture")
    environment = os.environ | baseline["environment"]
    environment["TMP"] = environment["TEMP"] = str(out / "temp")
    pending = ["tests/csol-skull-balrog/skull1_prediction.cpp"]
    sources = {}
    while pending:
        relative = pending.pop()
        if relative in sources:
            continue
        original = ROOT / relative
        frozen = out / "source" / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        frozen.write_bytes(original.read_bytes())
        sources[relative] = dict(original=identity(original), frozen=identity(frozen))
        for token in re.findall(r'^\s*#include\s+"([^"]+)"', frozen.read_text("utf-8-sig"), re.M):
            child = (original.parent / token).resolve()
            if child.is_file() and child.is_relative_to(ROOT) and not child.is_relative_to(ROOT / "external"):
                pending.append(child.relative_to(ROOT).as_posix())
    old = out / "old-skull1.cpp"
    old.write_bytes(args.old_source.read_bytes())
    steps = []

    def execute(label, command, expected=0):
        start = time.monotonic()
        completed = subprocess.run(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=180)
        log = out / (label + ".log")
        log.write_bytes(completed.stdout)
        includes = sorted(set(re.findall(r'Note: including file:\s*(.+)', completed.stdout.decode("utf-8", "replace"))))
        steps.append(dict(name=label, argv=command, exit=completed.returncode, expected=expected,
                          seconds=time.monotonic() - start, log=identity(log),
                          includes=[identity(path.strip()) for path in includes]))
        write(out / "steps.json", steps)
        print(label, completed.returncode, "expected", expected, flush=True)
        if completed.returncode != expected:
            print(completed.stdout.decode("utf-8", "replace")[-5000:])
            raise RuntimeError(label + " failed")

    for variant in ("new", "old"):
        command = []
        for arg in template:
            if arg == "/DTEST_SKULL1" or arg.startswith("/IH:") and "generated" in arg:
                continue
            if arg.startswith("/Fo"):
                arg = "/Fo" + str(out / (variant + ".obj"))
            elif arg.startswith("/Fd"):
                arg = "/Fd" + str(out / (variant + ".pdb"))
            elif arg == template[-1]:
                arg = str(out / "source/tests/csol-skull-balrog/skull1_prediction.cpp")
            command.append(arg)
        if variant == "old":
            logical = out / "source/native/csol/client/weapons/skull1.cpp"
            overlay = out / "old-overlay.json"
            write(overlay, dict(version=0, **{"case-sensitive": False, "use-external-names": False}, roots=[
                dict(type="file", name=str(logical), **{"external-contents": str(old)})]))
            command[command.index("--"):command.index("--")] = ["/clang:-ivfsoverlay", "/clang:" + str(overlay)]
        execute("compile-" + variant, command)
        exe = out / (variant + ".exe")
        execute("link-" + variant, [template[0], "--target=i686-pc-windows-msvc", "-fuse-ld=lld", "/MT",
                str(out / (variant + ".obj")), "/Fe" + str(exe), "/link", "/OPT:REF", "/OPT:LLDLTO=3", "/threads:1"])
        execute("run-" + variant, [str(exe), str(args.oracle.resolve())], 1 if variant == "old" else 0)
    changed = [relative for relative, row in sources.items()
               if identity(row["frozen"]["path"])["sha256"] != row["frozen"]["sha256"]]
    write(out / "result.json", dict(success=not changed, sources=sources, steps=steps, changed=changed,
        oracle=identity(args.oracle), oracleMetadata=identity(args.oracle.with_suffix(".json")),
        baseline=identity(args.baseline), oldSource=identity(old), runner=identity(__file__),
        deployed=False, liveGameplay=False,
        boundary="Full production predictor and real SDK types; controlled engine callbacks/CRT draws. No native CRT resolution or live gameplay claim."))
    assert not changed


if __name__ == "__main__":
    main()
