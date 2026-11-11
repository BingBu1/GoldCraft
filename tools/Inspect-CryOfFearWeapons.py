#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read original CoF models and PE identities without modifying the installation.

This is an inventory, not evidence of weapon behavior. IDA call paths must
establish the consumers before restoration code or generated poses are added.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def identity(path, data):
    return {"path": str(path), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def text(data):
    return data.split(b"\0", 1)[0].decode("ascii", "strict")


def records(data, offset, count, width):
    if offset < 0 or count < 0 or width <= 0 or count > (len(data) - offset) // width:
        raise ValueError("Studio table exceeds file bounds")
    return range(offset, offset + count * width, width)


def model(path):
    data = path.read_bytes()
    if len(data) < 244 or data[:4] != b"IDST" or struct.unpack_from("<i", data, 4)[0] != 10:
        raise ValueError(f"Expected GoldSrc Studio v10: {path}")
    if struct.unpack_from("<i", data, 72)[0] != len(data):
        raise ValueError(f"Studio length differs: {path}")
    count, offset = struct.unpack_from("<ii", data, 140)
    bones = []
    for at in records(data, offset, count, 112):
        bones.append({"name": text(data[at:at + 32]),
                      "parent": struct.unpack_from("<i", data, at + 32)[0],
                      "value": struct.unpack_from("<6f", data, at + 64),
                      "scale": struct.unpack_from("<6f", data, at + 88)})
    count, offset = struct.unpack_from("<ii", data, 164)
    sequences = []
    for at in records(data, offset, count, 176):
        events, event_offset = struct.unpack_from("<ii", data, at + 48)
        sequences.append({
            "index": len(sequences), "label": text(data[at:at + 32]),
            "fps": struct.unpack_from("<f", data, at + 32)[0],
            "flags": struct.unpack_from("<i", data, at + 36)[0],
            "frames": struct.unpack_from("<i", data, at + 56)[0],
            "blends": struct.unpack_from("<i", data, at + 120)[0],
            "group": struct.unpack_from("<i", data, at + 156)[0],
            "events": [{"frame": struct.unpack_from("<i", data, ev)[0],
                        "event": struct.unpack_from("<i", data, ev + 4)[0],
                        "type": struct.unpack_from("<i", data, ev + 8)[0],
                        "options": text(data[ev + 12:ev + 76])}
                       for ev in records(data, event_offset, events, 76)]})
    count, offset = struct.unpack_from("<ii", data, 180)
    textures = [{"name": text(data[at:at + 64]),
                 "flags": struct.unpack_from("<i", data, at + 64)[0],
                 "size": struct.unpack_from("<ii", data, at + 68)}
                for at in records(data, offset, count, 80)]
    return identity(path, data) | {"name": text(data[8:72]), "bones": bones,
                                    "sequences": sequences, "textures": textures,
                                    "bodyparts": struct.unpack_from("<i", data, 204)[0]}


def pe(path):
    data = path.read_bytes()
    at = struct.unpack_from("<I", data, 60)[0]
    if data[:2] != b"MZ" or data[at:at + 4] != b"PE\0\0":
        raise ValueError(f"Expected PE: {path}")
    machine, sections = struct.unpack_from("<HH", data, at + 4)
    optional = at + 24
    if machine != 0x14c or struct.unpack_from("<H", data, optional)[0] != 0x10b:
        raise ValueError(f"Expected x86 PE32: {path}")
    base = struct.unpack_from("<I", data, optional + 28)[0]
    section_at = optional + struct.unpack_from("<H", data, at + 20)[0]
    table = [struct.unpack_from("<IIII", data, section_at + 40 * i + 8)
             for i in range(sections)]

    def offset(rva):
        for virtual_size, virtual_at, raw_size, raw_at in table:
            if virtual_at <= rva < virtual_at + max(virtual_size, raw_size):
                return raw_at + rva - virtual_at
        raise ValueError("RVA outside PE sections")

    exports = []
    export_rva, export_size = struct.unpack_from("<II", data, optional + 96)
    if export_rva:
        exp = offset(export_rva)
        ordinal_base, functions, names, fun_rva, name_rva, ord_rva = struct.unpack_from("<6I", data, exp + 16)
        fun_at, name_at, ord_at = offset(fun_rva), offset(name_rva), offset(ord_rva)
        for index in range(names):
            name = offset(struct.unpack_from("<I", data, name_at + 4 * index)[0])
            ordinal = struct.unpack_from("<H", data, ord_at + 2 * index)[0]
            if ordinal >= functions:
                raise ValueError("Export ordinal outside function table")
            rva = struct.unpack_from("<I", data, fun_at + 4 * ordinal)[0]
            exports.append({"name": text(data[name:data.index(b"\0", name)]),
                            "rva": f"0x{rva:x}", "va": f"0x{base + rva:x}",
                            "ordinal": ordinal_base + ordinal,
                            "forwarded": export_rva <= rva < export_rva + export_size})
    return identity(path, data) | {"machine": "x86", "imageBase": f"0x{base:x}",
                                 "exports": exports}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if not output.is_relative_to(ROOT / "analysis") or output.exists():
        raise ValueError("Use a new JSON file in workspace analysis")
    models = [model(path) for path in sorted((source / "models/weapons").rglob("*.mdl"))]
    modules = [pe(source / "cl_dlls" / name) for name in ("hl.dll", "client.dll")]
    result = {"source": str(source), "models": models, "modules": modules,
              "boundary": "Read-only resource/symbol inventory; behavior requires original IDA consumers."}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"models": len(models), "modules": [
        {k: module[k] for k in ("path", "sha256", "imageBase")} | {"exports": len(module["exports"])}
        for module in modules], "output": str(output)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
