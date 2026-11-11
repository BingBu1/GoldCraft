"""Stage character MDLs from an identity-checked corpus; never deploy resources.

The corpus contains rows with alias, source, sequences and derived identities
(path/bytes/sha256). It must describe original MDL/SEQ inputs and the installed
baseline. Outputs use existing aliases; materials and catalogs are unchanged.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def identity(path):
    path = Path(path).resolve(strict=True)
    before = path.stat()
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f"Input changed during hashing: {path}")
    return dict(path=str(path), bytes=after.st_size, sha256=digest)


def verify(row):
    actual = identity(row["path"])
    if actual["bytes"] != row["bytes"] or actual["sha256"] != row["sha256"]:
        raise ValueError(f"Input identity mismatch: {row['path']}")
    return actual


def save(path, data):
    # This is a new staging directory, never an installed or hard-linked file.
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def descriptor_semantics(data):
    if len(data) < 244:
        raise ValueError("Short studio header")
    count, offset = struct.unpack_from("<ii", data, 164)
    if count < 1 or count > 2048 or offset < 244 or offset + count * 176 > len(data):
        raise ValueError("Invalid sequence descriptor range")
    records = []
    for i in range(count):
        record = bytearray(data[offset + i * 176:offset + (i + 1) * 176])
        for relocated in (52, 64, 124):
            record[relocated:relocated + 4] = b"\0" * 4
        records.append(bytes(record))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--packer", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--alias", action="append", help="Only stage these aliases; default all corpus rows")
    parser.add_argument("--append-only", action="store_true", help="Keep animation data appended instead of reusing the original arena")
    parser.add_argument("--run", action="store_true", help="Without this flag only prepare a plan")
    args = parser.parse_args()
    output = Path(os.path.abspath(args.output))
    for parent in (output, *output.parents):
        if parent.exists() and getattr(parent.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Output path traverses a reparse point: {parent}")
        if parent.is_symlink():
            raise ValueError(f"Output path traverses a link: {parent}")
    output = output.resolve()
    if not output.is_relative_to((ROOT / "build").resolve()) or output.exists():
        raise ValueError("Use a new staging directory inside workspace build")
    corpus_identity = identity(args.corpus)
    corpus = json.loads(args.corpus.read_text(encoding="utf-8-sig"))
    packer = identity(args.packer)
    catalog_path = ROOT / "native/csol/config/characters.json"
    catalog_identity = identity(catalog_path)
    if any(corpus["catalog"][k] != catalog_identity[k] for k in ("bytes", "sha256")):
        raise ValueError("Corpus belongs to a different character catalog")
    catalog = json.loads(catalog_path.read_text(encoding="utf-8-sig"))
    sources = {}
    for character in catalog["characters"]:
        alias, source = character["model"], character["sourceModel"]
        if alias in sources and sources[alias] != source:
            raise ValueError(f"Catalog alias has conflicting source models: {alias}")
        sources[alias] = source
    known = set(sources)
    requested = set(args.alias or (r["alias"] for r in corpus["rows"]))
    rows = [r for r in corpus["rows"] if r["alias"] in requested]
    if not rows or len(rows) != len(requested) or {r["alias"] for r in rows} != requested:
        raise ValueError("Empty, missing or duplicate corpus aliases")
    inputs = {}
    plan_rows = []
    for row in rows:
        alias = row["alias"]
        if alias not in known or not re.fullmatch(r"[a-zA-Z0-9_]+", alias):
            raise ValueError(f"Unknown/unsafe alias: {alias}")
        target = f"models/player/{alias}/{alias}.mdl"
        if len(target.encode("ascii")) >= 64:
            raise ValueError("Model path exceeds engine resource limit")
        def matches(item, relative):
            return Path(item["path"]).as_posix().casefold().endswith("/" + relative.replace("\\", "/").casefold())
        if not matches(row["source"], sources[alias]) or not matches(row["derived"], target):
            raise ValueError(f"Corpus source/baseline does not belong to alias: {alias}")
        if not row["sequences"]:
            raise ValueError(f"Missing sequence list: {alias}")
        for number, sequence in enumerate(row["sequences"], 1):
            if not matches(sequence, sources[alias][:-4] + str(number) + ".seq"):
                raise ValueError(f"Sequence order/source mismatch: {alias}")
        for item in (row["source"], *row["sequences"], row["derived"]):
            actual = verify(item)
            inputs[actual["path"]] = actual
        plan_rows.append(dict(alias=alias, target=target, source=row["source"],
                              sequences=row["sequences"], baseline=row["derived"]))
    output.mkdir(parents=True)
    plan = dict(corpus=corpus_identity, packer=packer, catalog=catalog_identity,
                script=identity(__file__), rows=plan_rows, inputs=list(inputs.values()),
                notInCorpus=sorted(known - {r["alias"] for r in corpus["rows"]}),
                selectedOnly=bool(args.alias), appendOnly=args.append_only, deployed=False)
    save(output / "plan.json", plan)
    if not args.run:
        print(json.dumps(dict(prepared=len(rows), compiled=False, deployed=False)))
        return 0
    result = dict(success=False, models=[], deployed=False)
    started = time.monotonic()
    try:
        for row in plan_rows:
            target = output / row["target"]
            command = [packer["path"], "--extended", *(["--append-only"] if args.append_only else []),
                       str(target), row["source"]["path"],
                       *(r["path"] for r in row["sequences"])]
            log = output / (row["alias"] + ".log")
            try:
                ran = subprocess.run(command, capture_output=True, timeout=120,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            except subprocess.TimeoutExpired as error:
                log.write_bytes((error.stdout or b"") + (error.stderr or b""))
                raise
            log.write_bytes(ran.stdout + ran.stderr)
            if ran.returncode:
                raise RuntimeError(f"Packer rejected {row['alias']}; see retained log")
            data = bytearray(target.read_bytes())
            if len(data) < 244 or data[:4] != b"IDST" or struct.unpack_from("<i", data, 4)[0] != 10:
                raise ValueError("Packer output is not decoded studio v10")
            if struct.unpack_from("<i", data, 72)[0] != len(data):
                raise ValueError("Packer output length mismatch")
            # Same cosmetic alias rule as Prepare-CSOLCharacters.py. This is a
            # newly generated independent staging file, never a linked asset.
            data[8:72] = (row["alias"] + ".mdl").encode("ascii").ljust(64, b"\0")
            target.write_bytes(data)
            baseline = Path(row["baseline"]["path"]).read_bytes()
            count = struct.unpack_from("<i", data, 164)[0]
            if count != struct.unpack_from("<i", baseline, 164)[0]:
                raise ValueError(f"Sequence count differs from installed alias: {row['alias']}")
            if descriptor_semantics(data) != descriptor_semantics(baseline):
                raise ValueError(f"Sequence semantics/order differ from installed alias: {row['alias']}")
            result["models"].append(dict(alias=row["alias"], target=row["target"],
                output=identity(target), baseline=row["baseline"], sequences=count,
                savedBytes=len(baseline)-len(data)))
            save(output / "result.json", result)
        for item in (plan["corpus"], plan["packer"], plan["catalog"], plan["script"], *inputs.values()):
            verify(item)
        result["inputsUnchanged"] = True
        result["savedBytes"] = sum(r["savedBytes"] for r in result["models"])
        result["success"] = True
    except Exception as error:
        result["error"] = str(error)
    finally:
        result["seconds"] = time.monotonic()-started
        save(output / "result.json", result)
    print(json.dumps({k: result[k] for k in ("success", "deployed", "seconds")}))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
