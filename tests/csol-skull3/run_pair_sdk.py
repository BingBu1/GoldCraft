#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze paired inventory tests against one verified complete server object set.

The parent must supply an ABI-consistent baseline with all player.h consumers
rebuilt. Optional overrides are reviewed frozen C++ sources, never whole mutable
external trees. This runner links a console SDK fixture, not a deployable DLL.
"""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import os
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(Path(path).read_text("utf-8-sig"))


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def key(path):
    return str(Path(path).resolve()).casefold()


def write(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2), "utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "template", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--overrides", type=Path)
    parser.add_argument("--groups", default=",".join(map(str, [*range(19), *range(20, 32), *range(40, 54), 60, 61])))
    args = parser.parse_args()
    base, template, out = args.baseline.resolve(), args.template.resolve(), args.output.resolve()
    assert out.is_relative_to(ROOT / "build") and not out.exists()
    result = read(base / "build-result.json")
    assert result["success"] and result["inputsUnchanged"]
    assert read(base / "dependency-audit.json")["success"]
    units = read(base / "complete-object-provenance.json")["units"]
    for unit in units:
        assert identity(unit["object"]["path"])["sha256"] == unit["object"]["sha256"]
    out.mkdir()
    (out / "temp").mkdir()
    runner_snapshot = out / Path(__file__).name
    runner_snapshot.write_bytes(Path(__file__).read_bytes())
    effective = {row["relative"]: row["frozen"] for row in read(base / "effective-source-manifest.json")["files"]}
    overlay = read(base / "source-overlay.json")
    roots = {key(row["name"]): row for row in overlay["roots"]}
    sources = {}
    replacements = {}
    for rel in ("native/csol/server/paired_inventory.cpp", "native/csol/server/weapons/skull3_ballistics.cpp"):
        replacements[rel] = identity(ROOT / rel)
    if args.overrides:
        for row in read(args.overrides):
            rel = row["relative"]
            if not rel.endswith(".cpp"):
                assert rel in ("native/csol/server/weapons/weapon_slot_position.h", "native/csol/server/weapons/wpn_skull3.h", "native/csol/server/weapons/weapon_call_guard.h")
                def declarations(path):
                    text = Path(path).read_text("utf-8-sig")
                    return [line.strip() for line in re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S).splitlines() if line.strip()]
                before = declarations(effective[rel]["path"]) if rel in effective else []
                after = declarations(row["frozen"]["path"])
                changed_lines = [line[2:] for line in difflib.ndiff(before, after) if line.startswith(("- ", "+ "))]
                if rel == "native/csol/server/weapons/weapon_call_guard.h":
                    assert rel not in effective, "An existing guard layout change requires a new full baseline"
                else:
                    assert all(line in ("#pragma once", "class CBasePlayerItem;") or re.fullmatch(r'void CSOL_Remember(?:Skull3Position|WeaponSlotPosition)\(CBasePlayerItem& item\);', line) for line in changed_lines), "Header layout changes require a new full baseline"
            assert identity(row["frozen"]["path"])["sha256"] == row["frozen"]["sha256"]
            replacements[rel] = row["frozen"]
    pending = ["tests/csol-skull3/pair_sdk.cpp"]
    fixtures = {}
    while pending:
        rel = pending.pop()
        if rel in fixtures:
            continue
        src = ROOT / rel
        fixtures[rel] = identity(src)
        for token in re.findall(r'^\s*#include\s+"([^"]+)"', src.read_text("utf-8-sig"), re.M):
            child = (src.parent / token).resolve()
            if child.is_file() and child.is_relative_to(ROOT / "tests"):
                pending.append(child.relative_to(ROOT).as_posix())
    for name in ("skull1", "skull2", "skull3", "skull5", "m249ex", "balrog11"):
        rel = f"native/csol/config/{name}_damage.json"
        fixtures[rel] = identity(ROOT / rel)
    for name in ("engine_edge_names.inc", "engine_edge_defaults.inc"):
        rel = "tests/csol-scenario-rules-session/" + name
        fixtures[rel] = identity(template / "source" / rel)
    for rel, original in (replacements | fixtures).items():
        dst = out / "source" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(Path(original["path"]).read_bytes())
        sources[rel] = dict(original=original, frozen=identity(dst))
        effective[rel] = sources[rel]["frozen"]
    # Replace all historical physical/virtual aliases of a changed file. A
    # frozen production source's sibling includes keep the complete baseline.
    for mapping in roots.values():
        normalized = mapping["name"].replace("\\", "/").casefold()
        for rel, row in sources.items():
            if normalized.endswith("/" + rel.casefold()):
                mapping["external-contents"] = row["frozen"]["path"]
                break
    for rel, row in effective.items():
        for name in (ROOT / rel, out / "source" / rel, Path(row["path"])):
            roots[key(name)] = dict(type="file", name=str(name), **{"external-contents": row["path"]})
    overlay["roots"] = list(roots.values())
    write(out / "overlay.json", overlay)
    known = {key(row["path"]): row for row in read(base / "inputs.json")["files"]}
    for row in effective.values():
        known[key(row["path"])] = row
    prior = read(template / "prepared.json")
    for row in prior["receipt"]["inputs"]:
        if not Path(row["path"]).is_relative_to(ROOT):
            assert identity(row["path"])["sha256"] == row["sha256"]
            known[key(row["path"])] = row
    command = list(prior["compile"])
    for i, token in enumerate(command):
        if token.endswith(".cpp"):
            command[i] = sources["tests/csol-skull3/pair_sdk.cpp"]["frozen"]["path"]
        elif token.startswith("/Fo"):
            command[i] = "/Fo" + str(out / "pair_sdk.obj")
        elif token.startswith("/clang:") and token.endswith("overlay.json"):
            command[i] = "/clang:" + str(out / "overlay.json")
    command.append("/Y-")
    jobs = []
    object_replacements = {}
    new_objects = []
    for rel in replacements:
        if not rel.endswith(".cpp"):
            continue
        matching = [unit for unit in units if unit["source"]["path"].replace("\\", "/").casefold().endswith("/" + rel.casefold())]
        assert len(matching) == 1 or (not matching and rel == "native/csol/server/weapons/weapon_call_guard.cpp"), rel
        name = Path(rel).stem
        cmd = list(command)
        for i, token in enumerate(cmd):
            if token.endswith(".cpp"):
                cmd[i] = sources[rel]["frozen"]["path"]
            elif token.startswith("/Fo"):
                cmd[i] = "/Fo" + str(out / (name + ".obj"))
        jobs.append((name, cmd))
        if matching:
            object_replacements[key(matching[0]["object"]["path"])] = str(out / (name + ".obj"))
        else:
            new_objects.append(str(out / (name + ".obj")))
    jobs.append(("pair_sdk", command))
    link = list(next(job["command"] for job in read(base / "build-commands.json") if job["kind"] == "link"))
    remove = ("/out:", "/pdb:", "/implib:", "/def:", "/ltcgout:", "/manifest", "/natvis:", "--intdir=")
    link = [token for token in link if token.casefold() != "/dll" and not token.casefold().startswith(remove)]
    link = [object_replacements.get(key(token), token) for token in link]
    link += new_objects + ["/OUT:" + str(out / "pair_sdk.exe"), "/PDB:" + str(out / "pair_sdk.pdb"), "/SUBSYSTEM:CONSOLE", str(out / "pair_sdk.obj"),
             "/lldltocache:" + str(out.parent / "pair-sdk-lto-cache")]
    jobs.append(("link", link))
    observed = {}
    for token in link:
        if Path(token).is_absolute() and Path(token).is_file() and Path(token).suffix.casefold() in (".obj", ".lib", ".res"):
            observed[key(token)] = identity(token)
    for row in sources.values():
        observed[key(row["frozen"]["path"])] = row["frozen"]
    for path in (out / "overlay.json", runner_snapshot, base / "complete-object-provenance.json", base / "build-result.json"):
        observed[key(path)] = identity(path)
    for group in args.groups.split(","):
        jobs.append(("execute-" + group, [str(out / "pair_sdk.exe"), group]))
    environment = os.environ | read(base / "environment.json")
    environment["TMP"] = environment["TEMP"] = str(out / "temp")
    receipt = dict(success=False, sources=sources, baseline=identity(base / "build-result.json"), steps=[],
                   deployed=False, liveGameplay=False, boundary="Actual complete server SDK/weapon/service lifecycle with controlled engine and actual ReAPI hooks. No live process or rendered game evidence.")
    write(out / "prepared.json", dict(jobs=jobs, environment=read(base / "environment.json"), sources=sources))
    failed_tests = []
    try:
        for name, argv in jobs:
            raw = argv
            if name == "link":
                rsp = out / "link.rsp"
                rsp.write_text(subprocess.list2cmdline(argv[1:]), "utf-8")
                argv = [argv[0], "@" + str(rsp)]
            start = time.monotonic()
            done = subprocess.run(argv, cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
            log = out / (name + ".log")
            log.write_bytes(done.stdout)
            text = done.stdout.decode("utf-8", "replace")
            includes = sorted(set(key(path.strip()) for path in re.findall(r'Note: including file:\s*(.+)', text)))
            unknown = [path for path in includes if path not in known]
            changed = [path for path in includes if path in known and identity(path)["sha256"] != known[path]["sha256"]]
            for path in includes:
                if path in known:
                    observed[path] = known[path]
            receipt["steps"].append(dict(name=name, argv=raw, exit=done.returncode, seconds=time.monotonic() - start,
                                         includes=includes, unknown=unknown, changed=changed, log=identity(log)))
            write(out / "result.json", receipt)
            print(name, done.returncode, "unknown", len(unknown), "changed", len(changed), flush=True)
            if name.startswith("execute") or done.returncode:
                print(text[-4500:], flush=True)
            if name.startswith("execute") and done.returncode:
                failed_tests.append(name)
                continue
            if done.returncode or unknown or changed:
                print(unknown, changed, flush=True)
                raise RuntimeError(name + " failed")
        receipt["failedTests"] = failed_tests
        if failed_tests:
            raise RuntimeError("SDK groups failed: " + ", ".join(failed_tests))
        receipt["success"] = True
    finally:
        receipt["observedInputs"] = list(observed.values())
        receipt["changedAfter"] = [row["path"] for row in observed.values() if identity(row["path"])["sha256"] != row["sha256"]]
        receipt["success"] &= not receipt["changedAfter"]
        write(out / "result.json", receipt)


if __name__ == "__main__":
    main()
