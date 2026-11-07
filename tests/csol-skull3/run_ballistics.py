#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze the real bullet28 TU and test it against original MP/real SDK damage.

The full-server baseline provides verified game objects, ABI and environment.
The shield fixture provides the already audited separate ReHLDS hull DLL and
model inputs. Neither receipt substitutes for running this production TU.
"""
from pathlib import Path
import argparse
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


def write(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), "utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "shield", "oracle", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--restored-inputs", type=Path)
    args = parser.parse_args()
    base, shield, out = args.baseline.resolve(), args.shield.resolve(), args.output.resolve()
    assert out.is_relative_to(ROOT / "build") and not out.exists()
    result = read(base / "build-result.json")
    assert result["success"] and result["inputsUnchanged"]
    assert read(shield / "run-result.json")["success"]
    oracle_meta = read(args.oracle.with_suffix(".json"))
    assert identity(args.oracle)["sha256"] == oracle_meta["sha256"]
    assert oracle_meta["moduleSha256"] == "39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2"
    out.mkdir()
    (out / "temp").mkdir()
    manifest = read(base / "effective-source-manifest.json")["files"]
    sources = {}
    pending = ["tests/csol-skull3/ballistics.cpp", "native/csol/server/weapons/skull3_ballistics.cpp", "native/csol/server/weapons/skull3_ballistics.h"]
    while pending:
        relative = pending.pop()
        if relative in sources:
            continue
        original = ROOT / relative
        frozen = out / "source" / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        frozen.write_bytes(original.read_bytes())
        assert frozen.stat().st_nlink == 1
        sources[relative] = dict(original=identity(original), frozen=identity(frozen))
        for token in re.findall(r'^\s*#include\s+"([^"]+)"', frozen.read_text("utf-8-sig"), re.M):
            child = (original.parent / token).resolve()
            if child.is_file() and child.is_relative_to(ROOT / "tests"):
                pending.append(child.relative_to(ROOT).as_posix())
    abi = next(row for row in read(base / "inputs.json")["files"]
               if row["path"].replace("\\", "/").casefold().endswith("/regamedll_cs/regamedll/engine/eiface.h"))
    assert identity(abi["path"])["sha256"] == abi["sha256"]
    body = Path(abi["path"]).read_text("utf-8-sig").split("typedef struct enginefuncs_s", 1)[1].split("} enginefuncs_t;", 1)[0]
    edges = re.findall(r'\(\s*\*\s*(pfn\w+)\s*\)', re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S))
    assert len(edges) == len(set(edges)) and edges[0] == "pfnPrecacheModel" and edges[-1] == "pfnEngCheckParm"
    generated = {
        "engine_edge_names.inc": "inline constexpr const char* engineEdgeNames[] = {\n" + ''.join('    "' + n + '",\n' for n in edges) + '};\n',
        "engine_edge_defaults.inc": ''.join(f'g_engfuncs.{n}=Unsupported<{i},decltype(g_engfuncs.{n})>::Call;\n' for i, n in enumerate(edges)),
    }
    for name, contents in generated.items():
        rel = "tests/csol-scenario-rules-session/" + name
        path = out / "source" / rel
        path.write_text(contents, "utf-8")
        sources[rel] = dict(frozen=identity(path), generatedFrom=abi)
    overlay = read(base / "source-overlay.json")
    restored = {}
    for row in read(args.restored_inputs) if args.restored_inputs else []:
        key = str(Path(row["original"]).resolve()).casefold()
        assert identity(row["replacement"])["sha256"] == row["sha256"]
        expected = next(r for r in read(base / "inputs.json")["files"] if str(Path(r["path"]).resolve()).casefold() == key)
        assert row["sha256"] == expected["sha256"]
        frozen = out / "restored-inputs" / row["sha256"] / Path(row["original"]).name
        frozen.parent.mkdir(parents=True, exist_ok=True)
        frozen.write_bytes(Path(row["replacement"]).read_bytes())
        restored[key] = dict(expected=expected, frozen=identity(frozen), recoveredFrom=identity(row["replacement"]))
    for row in overlay["roots"]:
        key = str(Path(row["external-contents"]).resolve()).casefold()
        if key in restored:
            row["external-contents"] = restored[key]["frozen"]["path"]
    roots = {str(row["name"]).casefold(): row for row in overlay["roots"]}
    for row in manifest:
        name = str(out / "source" / row["relative"])
        roots[name.casefold()] = dict(type="file", name=name, **{"external-contents": row["frozen"]["path"]})
    for rel, row in sources.items():
        for path in (ROOT / rel, out / "source" / rel):
            roots[str(path).casefold()] = dict(type="file", name=str(path), **{"external-contents": row["frozen"]["path"]})
    for key, row in restored.items():
        roots[key] = dict(type="file", name=row["expected"]["path"], **{"external-contents": row["frozen"]["path"]})
    overlay["roots"] = list(roots.values())
    write(out / "overlay.json", overlay)
    prepared = read(shield / "prepared.json")
    template = next(job["command"] for job in prepared["jobs"] if job["name"] == "compile-new")
    command = []
    for token in template:
        if token.startswith("/Fo"):
            token = "/Fo" + str(out / "ballistics.obj")
        elif token.startswith("/clang:") and token.endswith("overlay.json"):
            token = "/clang:" + str(out / "overlay.json")
        elif token.endswith("server.cpp"):
            token = str(out / "source/tests/csol-skull3/ballistics.cpp")
        command.append(token)
    link = list(next(row["command"] for row in read(base / "build-commands.json") if row["kind"] == "link"))
    remove = ("/out:", "/pdb:", "/implib:", "/def:", "/ltcgout:", "/manifest", "/natvis:", "--intdir=")
    link = [token for token in link if token.lower() != "/dll" and not token.lower().startswith(remove)]
    link += ["/OUT:" + str(out / "ballistics.exe"), "/PDB:" + str(out / "ballistics.pdb"), "/SUBSYSTEM:CONSOLE", str(out / "ballistics.obj")]
    link += ["/lldltocache:" + str(out.parent / "ballistics-sdk-lto-cache")]
    environment = os.environ | read(base / "environment.json")
    environment["TMP"] = environment["TEMP"] = str(out / "temp")
    known = {}
    for row in read(base / "inputs.json")["files"] + [row["frozen"] for row in manifest] + [row["frozen"] for row in sources.values()]:
        path = Path(row["path"])
        key = str(path.resolve()).casefold()
        if key in restored:
            row = restored[key]["frozen"]
            path = Path(row["path"])
        assert identity(path)["sha256"] == row["sha256"], str(path)
        known[str(path.resolve()).casefold()] = row
    for path in environment["INCLUDE"].split(";"):
        directory = Path(path)
        if directory.is_dir() and (directory / "filesystem").is_file():
            for item in directory.rglob("*"):
                if item.is_file():
                    known[str(item.resolve()).casefold()] = identity(item)
    models = read(shield / "models.json")
    dependencies = [identity(token) for token in link if Path(token).is_absolute() and Path(token).is_file() and Path(token).suffix.lower() in (".obj", ".lib", ".res")]
    for row in dependencies + models["engine"] + [row["copy"] for row in models["models"]] + [identity(args.oracle), identity(args.oracle.with_suffix(".json")), identity(out / "overlay.json")]:
        assert identity(row["path"])["sha256"] == row["sha256"]
        known[str(Path(row["path"]).resolve()).casefold()] = row
    receipt = dict(success=False, sources=sources, restoredInputs=restored, inputs=list(known.values()), baseline=identity(base / "build-result.json"),
                   oracle=identity(args.oracle), oracleMetadata=oracle_meta, dependencies=dependencies, commands=[],
                   deployed=False, liveGameplay=False,
                   boundary="Production bullet28 and complete native SDK damage; original instruction oracle, controlled engine trace sequences, actual pooled monsters and independent ReHLDS model hull DLL. Weapon wrapper lifecycle and live gameplay are separate.")
    write(out / "prepared.json", dict(compile=command, link=link, environment=read(base / "environment.json"), receipt=receipt))
    steps = [("compile", command), ("link", link),
             ("oracle", [str(out / "ballistics.exe"), "oracle", str(args.oracle.resolve())]),
             ("monsters", [str(out / "ballistics.exe"), "monsters", str(shield / "data")]),
             ("lifecycle-reset", [str(out / "ballistics.exe"), "lifecycle-reset", "unused"]),
             ("lifecycle-release", [str(out / "ballistics.exe"), "lifecycle-release", "unused"])]
    for name, argv in steps:
        original_argv = argv
        if name == "link":
            rsp = out / "link.rsp"
            rsp.write_text(subprocess.list2cmdline(argv[1:]), "utf-8")
            argv = [argv[0], "@" + str(rsp)]
        start = time.monotonic()
        done = subprocess.run(argv, cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
        log = out / (name + ".log")
        log.write_bytes(done.stdout)
        text = done.stdout.decode("utf-8", "replace")
        includes = sorted(set(str(Path(path.strip()).resolve()).casefold() for path in re.findall(r'Note: including file:\s*(.+)', text)))
        unknown = [path for path in includes if path not in known]
        changed = [path for path in includes if path in known and identity(path)["sha256"] != known[path]["sha256"]]
        receipt["commands"].append(dict(name=name, argv=original_argv, exit=done.returncode, seconds=time.monotonic() - start,
                                        log=identity(log), includes=includes, unknown=unknown, changed=changed))
        write(out / "result.json", receipt)
        print(name, done.returncode, "unknown", len(unknown), "changed", len(changed), flush=True)
        if done.returncode or unknown or changed:
            print(text[-7000:], unknown, changed, flush=True)
            raise RuntimeError(name + " failed")
        if name not in ("compile", "link"):
            print(text[-3500:], flush=True)
    receipt["changedAfter"] = [row["path"] for row in known.values() if identity(row["path"])["sha256"] != row["sha256"]]
    receipt["success"] = not receipt["changedAfter"]
    write(out / "result.json", receipt)
    assert receipt["success"]


if __name__ == "__main__":
    main()
