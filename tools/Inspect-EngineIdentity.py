"""Record PE identity and CRC-64/XZ used by the supplied MetaHook symbol catalog."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def crc64_xz(data):
    table = []
    for byte in range(256):
        value = byte
        for _ in range(8):
            value = (value >> 1) ^ (0xC96C5795D7870F42 if value & 1 else 0)
        table.append(value)
    crc = 0xFFFFFFFFFFFFFFFF
    for byte in data:
        crc = table[(crc ^ byte) & 255] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFFFFFFFFFF


def inspect(path):
    path = path.resolve(strict=True)
    if not path.is_relative_to(ROOT):
        raise ValueError("Inspect a workspace copy, not the original installation")
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("Not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, sections, timestamp = struct.unpack_from("<HHI", data, pe + 4)
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic != 0x10B:
        raise ValueError("GoldSrc engine should be PE32")
    return {
        "path": str(path), "bytes": len(data),
        "machine": hex(machine), "sections": sections, "peTimestamp": timestamp,
        "preferredImageBase": hex(struct.unpack_from("<I", data, optional + 28)[0]),
        "imageBytes": struct.unpack_from("<I", data, optional + 56)[0],
        "crc64xz": f"{crc64_xz(data):016X}", "sha256": hashlib.sha256(data).hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binaries", nargs="+", type=Path)
    args = parser.parse_args()
    assert crc64_xz(b"123456789") == 0x995DC9BBDF1939FA
    evidence = {"algorithm": "CRC-64/XZ", "modules": [inspect(p) for p in args.binaries]}
    target = ROOT / "analysis" / "engine" / "module-identities.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
