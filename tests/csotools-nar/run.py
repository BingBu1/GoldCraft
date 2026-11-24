"""Check a CSOToolLib install with small NAR fixtures. Run on Windows with .NET 2.0."""
import argparse
import bz2
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

ORIGINAL = 0x01000000
REPACKED = 0x4F30D5BD
SIGNATURE = 0x0052414E
DIRECTORY_KEY = bytes.fromhex("19 5b 7b 2c 65 5e 79 25 6e 4b 07 21 62 7f 00 29")


def xor(data, key):
    return bytes(value ^ key[i % len(key)] for i, value in enumerate(data))


def path_key(name):
    seed = 0
    for char in name:
        seed = ((seed * 1000003) ^ char) & 0xffffffff
    seed ^= len(name)
    key = bytearray()
    for _ in range(16):
        seed = (seed * 1103515245 + 12345) & 0xffffffff
        key.append(seed & 255)
    return key


def literal(data):
    blocks = (data[i:i + 32] for i in range(0, len(data), 32))
    return b"".join(bytes([len(block) - 1]) + block for block in blocks)


def archive(version, entries, license_alias=False):
    payload = bytearray()
    rows = []
    for text, method, stored, size in entries:
        name = text.encode("cp949")
        if method:
            stored = xor(stored, path_key(name))
        metadata = (8 + len(payload), len(stored), size, 0, zlib.crc32(stored))
        rows.append((name, method, metadata))
        if license_alias and name == b"/LICENSE":
            rows.insert(0, (b"//LICENSE", 2, metadata))
        payload.extend(stored)
    directory = bytearray(struct.pack("<5I", 1, 0, 0, 1, len(rows)))
    for name, method, metadata in rows:
        directory.extend(struct.pack("<H", len(name)) + name + struct.pack("<iIiiII", method, *metadata))
    directory = xor(bz2.compress(directory), DIRECTORY_KEY)
    footer = SIGNATURE if version == ORIGINAL else 0
    return (struct.pack("<II", SIGNATURE, version) + payload + directory
            + struct.pack("<II", len(directory) ^ 0x4076551F, footer))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool_dir", type=Path, help="directory containing the reader and its SharpZipLib")
    args = parser.parse_args()
    test_dir = Path(__file__).resolve().parent
    build = test_dir.parents[1] / "build"
    build.mkdir(exist_ok=True)
    compiler = Path(os.environ["WINDIR"]) / "Microsoft.NET/Framework/v2.0.50727/csc.exe"
    with tempfile.TemporaryDirectory(dir=build, prefix="csotools-test-") as temp:
        work = Path(temp)
        for name in ("CSOToolLib.dll", "ICSharpCode.SharpZipLib.dll"):
            shutil.copyfile(args.tool_dir / name, work / name)
        license_entry = ("/LICENSE", 1, b"license", 7)
        files = [license_entry, ("/dir/q?.txt", 1, b"question", 8),
                 ("/dir/q%3F.txt", 1, b"percent", 7), ("/dir/샷건.bin", 1, b"bitmap", 6)]
        repacked = archive(REPACKED, files, license_alias=True)
        (work / "repacked.nar").write_bytes(repacked)
        distinct = [license_entry, ("//LICENSE", 2, literal(b"another"), 7)]
        (work / "distinct.nar").write_bytes(archive(REPACKED, distinct))
        # The last distance-one back-reference yields eight bytes across read #1 and read #2.
        standard = archive(ORIGINAL, [("/dir/ref.bin", 2, literal(b"A" * 8190) + b"\xc0\x00", 8198)])
        (work / "standard.nar").write_bytes(standard)
        for name, data in {
            "standard-zero": standard[:-4] + bytes(4),
            "repacked-nar": repacked[:-4] + struct.pack("<I", SIGNATURE),
            "unknown": repacked[:4] + b"ABCD" + repacked[8:],
            "short": repacked[:10],
            "truncated": repacked[:-3],
        }.items():
            (work / (name + ".nar")).write_bytes(data)
        subprocess.run([str(compiler), "/nologo", "/r:CSOToolLib.dll", "/out:Check.exe",
                        str(test_dir / "Check.cs")], cwd=work, check=True)
        subprocess.run([str(work / "Check.exe")], cwd=work, check=True)


if __name__ == "__main__":
    main()
