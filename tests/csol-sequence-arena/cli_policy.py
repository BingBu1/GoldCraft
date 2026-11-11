#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Black-box exporter policy regression against identity-pinned original inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def identity(path):
    data = Path(path).read_bytes()
    return dict(path=str(Path(path).resolve()), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def word(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--packer", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    if not out.is_relative_to(root / "build") or out.exists():
        raise ValueError("Use a new workspace build directory")
    corpus = json.loads(args.corpus.read_text(encoding="utf-8-sig"))
    rows = {r["alias"]: r for r in corpus["rows"] if r["alias"] in ("csol87", "csol89")}
    if len(rows) != 2:
        raise ValueError("Required original fixtures are missing")
    inputs = [identity(p) for p in (args.corpus, args.packer, args.reference, __file__)]
    for row in rows.values():
        for item in (row["source"], *row["sequences"]):
            actual = identity(item["path"])
            if any(actual[k] != item[k] for k in ("bytes", "sha256")):
                raise ValueError("Original fixture identity changed")
            inputs.append(actual)
    out.mkdir(parents=True)
    result = dict(success=False, inputs=inputs, commands=[], liveGame=False)

    def pack(name, executable, alias, options, success):
        row = rows[alias]
        target = out / (name + ".mdl")
        command = [str(executable.resolve()), *options, str(target), row["source"]["path"],
                   *(s["path"] for s in row["sequences"])]
        ran = subprocess.run(command, capture_output=True, timeout=120,
                             creationflags=subprocess.CREATE_NO_WINDOW)
        (out / (name + ".log")).write_bytes(ran.stdout + ran.stderr)
        result["commands"].append(dict(name=name, argv=command, exit=ran.returncode))
        if (ran.returncode == 0) != success or target.exists() != success:
            raise AssertionError("Unexpected export result: " + name)
        return target.read_bytes() if success else b""

    try:
        basic = ["--extended", "--append-only"]
        pack("reference-strict", args.reference, "csol87", basic, False)
        pack("current-strict", args.packer, "csol87", basic, False)
        allowed = pack("guarded", args.packer, "csol87", [*basic, "--allow-unsupported-tracks"], True)
        reordered = pack("reordered", args.packer, "csol87",
                         ["--allow-unsupported-tracks", "--append-only", "--extended"], True)
        if allowed != reordered:
            raise AssertionError("Option order changed resource bytes")
        strict = pack("ready-strict", args.reference, "csol89", basic, True)
        ready = pack("ready-guarded", args.packer, "csol89", [*basic, "--allow-unsupported-tracks"], True)
        if strict != ready:
            raise AssertionError("Guarded policy changed an all-ready model")
        descriptors = word(allowed, 168)
        records = word(allowed, descriptors - 12)
        unsupported = [i for i in range(word(allowed, 164)) if word(allowed, records + i * 32 + 24) == 0]
        if unsupported != list(range(308, 353)):
            raise AssertionError("Unknown track set changed")
        index = word(Path(rows["csol87"]["source"]["path"]).read_bytes(), 164)
        preserved = 0
        for source in rows["csol87"]["sequences"]:
            data = Path(source["path"]).read_bytes()
            count, at = word(data, 0), word(data, 4)
            cursor = 8 + sum(word(data, at + i * 176 + 48) * 76 +
                             word(data, at + i * 176 + 60) * 20 for i in range(count))
            for i in range(count):
                end = at if i + 1 == count else cursor + word(data, at + (i + 1) * 176 + 124) - word(data, at + i * 176 + 124)
                if index + i in unsupported:
                    record = records + (index + i) * 32
                    start, size = word(allowed, record), word(allowed, record + 4)
                    if allowed[start:start + size] != data[cursor:end]:
                        raise AssertionError("Unsupported authored bytes changed")
                    preserved += size
                cursor = end
            index += count
        for item in inputs:
            if identity(item["path"]) != item:
                raise AssertionError("Test input changed")
        result.update(success=True, inputsUnchanged=True, unsupported=unsupported, preservedBytes=preserved)
    finally:
        (out / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("success", "inputsUnchanged", "preservedBytes")}))


if __name__ == "__main__":
    main()
