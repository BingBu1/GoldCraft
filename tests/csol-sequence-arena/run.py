#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Serial x86 animation-arena regression; immutable Git oracle is compiled via stdin."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OLD = "d39014de17dfcf7b665fe955fc5bc13c54be261e"
COMPILER_SHA = "dd5986f7acdcece0329ed801862b8fd81cd3afc69e21f21b03d5fd9ba9c02474"


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], creationflags=subprocess.CREATE_NO_WINDOW)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/csol-sequence-arena")
    parser.add_argument("--run", action="store_true", help="Compile/link/test; otherwise only prepare")
    parser.add_argument("--resources", choices=("none", "single", "all"), default="single")
    parser.add_argument("--original", action="store_true", help="Also execute the preserved formal test entry point")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("outputs must be inside workspace build")
    if out.exists() and any(out.iterdir()):
        raise ValueError("use a fresh output directory to preserve prior evidence")
    out.mkdir(parents=True, exist_ok=True)
    seed_path = ROOT / "build/engine-crc32/plan.json"
    env_path = ROOT / "analysis/map-transition-active-20261107/append/environment.json"
    seed = json.loads(seed_path.read_text(encoding="utf-8-sig"))
    compiler = Path(seed["commands"][0]["argv"][0])
    if identity(compiler)["sha256"] != COMPILER_SHA:
        raise ValueError("compiler identity mismatch")
    overrides = json.loads(env_path.read_text(encoding="utf-8-sig"))
    if not all(isinstance(k, str) and isinstance(v, str) for k, v in overrides.items()):
        raise ValueError("environment must be a string dictionary")
    environment = dict(os.environ)
    environment.update(overrides)
    environment["VSLANG"] = "1033"
    legacy = git("show", OLD + ":native/csol/sequences.cpp")
    if b"CanReuseAnimationArena" in legacy:
        raise ValueError("legacy oracle unexpectedly reuses arenas")
    flags = [str(compiler), "--target=i686-pc-windows-msvc", "/nologo", "/std:c++20", "/EHsc", "/MT", "/utf-8",
             "/O2", "/clang:-O3", "/clang:-flto=thin", "/arch:AVX2", "/Gy", "/Gw", "/fp:precise", "/Y-",
             "/showIncludes", "/DNOMINMAX", "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ROOT / "native/csol")]
    commands = []
    objects = []
    for name, source in (("legacy", None), ("current", ROOT / "native/csol/sequences.cpp"),
                         ("codec", ROOT / "native/csol/model_codec.cpp"), ("fixture", HERE / "fixture.cpp")):
        obj = out / (name + ".obj")
        objects.append(str(obj))
        extra = (["/DAppendSequences=OldAppendSequences", "/DSequenceFiles=OldSequenceFiles", "/TP", "-"]
                 if source is None else [str(source)])
        commands.append(dict(name="compile-" + name, argv=[*flags, "/c", *extra, "/Fo" + str(obj)], stdin=name == "legacy"))
    exe = out / "test.exe"
    commands.append(dict(name="link", argv=[str(compiler), "--target=i686-pc-windows-msvc", "-fuse-ld=lld", "/MT",
                    *objects, "/Fe" + str(exe), "/link", "/OPT:REF", "/OPT:ICF", "/OPT:LLDLTO=3", "/threads:1", "/verbose"]))
    commands.append(dict(name="synthetic", argv=[str(exe)]))
    corpus_path = ROOT / "analysis/map-transition-active-20261107/original/character-corpus.json"
    corpus = json.loads(corpus_path.read_text(encoding="utf-8-sig"))
    rows = ([] if args.resources == "none" else [r for r in corpus["rows"] if args.resources == "all" or r["alias"] == "csol89"])
    if args.resources == "single" and len(rows) != 1:
        raise ValueError("single-character evidence missing")
    resources = {}
    if args.original:
        common = Path(next(r for r in corpus["rows"] if r["alias"] == "csol89")["source"]["path"]).parents[3]
        for name in ("leet", "urban", "zombi_host"):
            folder = common / "models/player" / name
            for path in [folder / (name + ".mdl"), *folder.glob(name + "*.seq")]:
                item = identity(path)
                resources[item["path"]] = item
        commands.append(dict(name="original-entry", argv=[str(exe), "--original", str(common)]))
    for row in rows:
        for item in [row["source"], *row["sequences"]]:
            expected = {k: item[k] for k in ("path", "bytes", "sha256")}
            actual = identity(item["path"])
            if (os.path.normcase(actual["path"]) != os.path.normcase(str(Path(expected["path"]).resolve())) or
                    any(actual[k] != expected[k] for k in ("bytes", "sha256"))):
                raise ValueError("corpus resource identity mismatch: " + item["path"])
            resources[actual["path"]] = actual
        commands.append(dict(name="resource-" + row["alias"], argv=[str(exe), row["source"]["path"]]))
    sources = [HERE / "fixture.cpp", Path(__file__), ROOT / "native/csol/sequences.cpp",
               ROOT / "native/csol/model_codec.cpp", ROOT / "native/csol/sequence_tests.cpp"]
    sources.extend((ROOT / "native/csol").glob("*.hpp"))
    plan = dict(commands=commands, sources=[identity(p) for p in sources], compiler=identity(compiler),
                buildPlan=identity(seed_path), environment=identity(env_path), corpus=identity(corpus_path),
                legacy=dict(revision=OLD, sha256=hashlib.sha256(legacy).hexdigest(),
                            blob=git("rev-parse", OLD + ":native/csol/sequences.cpp").decode().strip(), savedSource=False),
                head=git("rev-parse", "HEAD").decode().strip(), resources=list(resources.values()),
                resourceAliases=[r["alias"] for r in rows], unavailableSourceHeaders=corpus["summary"]["unavailableSourceHeaders"],
                sourceTreesCopied=False, liveGame=False)
    write(out / "plan.json", plan)
    if not args.run:
        print(json.dumps(dict(prepared=str(out / "plan.json"), commands=len(commands), compiled=False)))
        return 0
    result = dict(success=False, commands=[], liveGame=False)
    started = time.monotonic()
    try:
        # Freeze candidate toolchain headers before any compiler runs. All actual
        # includes must appear here; an unexpected header is an audit failure.
        watch = {Path(r["path"]): r for r in plan["sources"]}
        roots = [Path(p) for p in overrides["INCLUDE"].split(";") if p and
                 ("MSVC" in p or "ucrt" in p)]
        roots.extend((compiler.parent.parent / "lib/clang").glob("*/include"))
        for root in roots:
            if root.exists():
                for p in root.rglob("*"):
                    if p.is_file():
                        p = p.resolve()
                        watch[p] = identity(p)
        write(out / "inputs-before.json", list(watch.values()))
        libraries = {}
        for root in [Path(p) for p in overrides["LIB"].split(";") if p]:
            if root.exists():
                for p in root.glob("*.lib"):
                    libraries[p.resolve()] = identity(p)
        write(out / "libraries-before.json", list(libraries.values()))
        includes = set()
        for command in commands:
            begin = time.monotonic()
            ran = subprocess.run(command["argv"], input=legacy if command.get("stdin") else None,
                                 cwd=out, env=environment, capture_output=True, timeout=600,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
            output = (ran.stdout + ran.stderr).decode("utf-8", errors="replace")
            log = out / (command["name"] + ".log")
            log.write_text(output, encoding="utf-8")
            result["commands"].append(dict(name=command["name"], exit=ran.returncode,
                                           seconds=time.monotonic()-begin, log=identity(log)))
            write(out / "result.json", result)
            print(command["name"], ran.returncode, flush=True)
            if ran.returncode:
                raise RuntimeError(command["name"] + " failed; see " + str(log))
            if command["name"].startswith("compile-"):
                includes.update(Path(line.split("including file:", 1)[1].strip()).resolve()
                                for line in output.splitlines() if "including file:" in line)
            if command["name"] == "link":
                result["executable"] = identity(exe)
                opens = set()
                members = set()
                for line in output.splitlines():
                    match = re.search(r"(?:^|:\s+)Reading (.+?\.lib)(?:\(([^\r\n]*)\))?\s*$", line, re.I)
                    if match:
                        if match[2] is not None:
                            members.add(match[1].lower())
                        else:
                            path = Path(match[1]).resolve(strict=True)
                            if path not in libraries:
                                raise ValueError("unregistered actual library: " + str(path))
                            opens.add(path)
                if not opens or any(sum(p.name.lower() == name for p in opens) != 1 for name in members):
                    raise ValueError("unresolved archive member or absent library trace")
                result["actualLibraries"] = [libraries[p] for p in sorted(opens)]
                result["librariesUnchanged"] = all(identity(p) == libraries[p] for p in opens)
        unknown = includes - watch.keys()
        if unknown:
            raise ValueError("unfrozen actual includes: " + str(sorted(map(str, unknown))))
        result["actualIncludes"] = [watch[p] for p in sorted(includes)]
        result["inputsUnchanged"] = all(identity(p) == row for p, row in watch.items())
        result["resourcesUnchanged"] = all(identity(p) == row for p, row in resources.items())
        result["compilerUnchanged"] = identity(compiler) == plan["compiler"]
        pe = exe.read_bytes()
        at = struct.unpack_from("<I", pe, 0x3c)[0]
        result["x86"] = pe[at:at+4] == b"PE\0\0" and struct.unpack_from("<H", pe, at+4)[0] == 0x14c
        result["objects"] = [identity(p) for p in objects]
        result["headAfter"] = git("rev-parse", "HEAD").decode().strip()
        result["success"] = all(result[k] for k in ("inputsUnchanged", "resourcesUnchanged", "compilerUnchanged", "librariesUnchanged", "x86"))
    except Exception as error:
        result["error"] = str(error)
    finally:
        result["seconds"] = time.monotonic() - started
        write(out / "result.json", result)
    print(json.dumps({k: result[k] for k in ("success", "seconds")}))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
