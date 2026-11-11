#!/usr/bin/env python3
"""Compare real XAudio2 voice parameters with SHA-pinned original instructions.

The original numeric call range executes unmodified. Only the engine's cvar,
RandomFloat and external audio-vtable providers are stubs. No original bytes
are distributed; supply the private client.dll.
"""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import struct
import subprocess

from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("cof_fov_oracle", ROOT / "tests/cryoffear-aim/oracle.py")
fov = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fov)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    original = fov.Original(args.sample)
    uc, data = original.uc, fov.DATA
    # cdecl RandomFloat(low,high): float x87 return, retaining input arguments.
    uc.mem_write(fov.STUB + 0x40, b"\xD9\x05" + fov.u32(data + 4) + b"\xC3")
    uc.mem_write(0x101B2714, fov.u32(fov.STUB + 0x40))
    # cdecl GetCvarFloat("volume") is the existing x87 stub at STUB+20.
    # External voice calls copy the passed float bit pattern, then ret4.
    for offset, target in ((0x80, data + 16), (0xA0, data + 20)):
        uc.mem_write(fov.STUB + offset, b"\x8B\x44\x24\x04\xA3" + fov.u32(target) + b"\xC2\x04\x00")
    uc.mem_write(fov.HUD + 0x100, fov.u32(fov.CVARS))
    uc.mem_write(0x105440AC, fov.u32(fov.HUD + 0x100))
    uc.mem_write(fov.CVARS + 0x20, fov.u32(fov.STUB + 0x80))
    uc.mem_write(fov.CVARS + 0x60, fov.u32(fov.STUB + 0xA0))
    rows = list(itertools.product((-10.0, -0.0, 0.0, 0.125, 0.5, 1.0, 2.0),
                                  (-0.1, -0.05, -0.0, 0.05, 0.1)))
    payload = b"".join(struct.pack("<2f", *row) for row in rows)
    expected = bytearray()
    for volume, random in struct.iter_unpack("<2f", payload):
        uc.mem_write(data + 8, struct.pack("<f", volume))
        uc.mem_write(data + 4, struct.pack("<f", random))
        uc.reg_write(UC_X86_REG_EBP, fov.STACK + 0x1000)
        uc.reg_write(UC_X86_REG_ESP, fov.STACK + 0x2000)
        uc.emu_start(0x10080572, 0x100805F4, count=100)
        expected += uc.mem_read(data + 16, 8)
    actual = subprocess.run([str(args.runner.resolve())], input=payload, capture_output=True, check=True).stdout
    assert len(actual) == len(expected) * 2
    mismatches, backend_differences = [], []
    requested, backend = bytearray(), bytearray()
    for i, (want, row) in enumerate(zip(struct.iter_unpack("<2I", expected), struct.iter_unpack("<4I", actual))):
        got, real = row[:2], row[2:]
        requested += struct.pack("<2I", *got)
        backend += struct.pack("<2I", *real)
        if want != got:
            mismatches.append(dict(case=i, original=list(want), requested=list(got)))
        if want != real:
            backend_differences.append(dict(case=i, original=list(want), realXAudio2=list(real)))
            assert want[0] == 0x80000000 and real[0] == 0 and want[1] == real[1], "Unexpected backend normalization"
    receipt = dict(sampleSha256=fov.SHA, originalRange="10080572-100805f4",
                   instructionSha256=hashlib.sha256(uc.mem_read(0x10080572, 0x82)).hexdigest(),
                   cases=len(rows), wordsPerCase=2, mismatchCount=len(mismatches), mismatches=mismatches,
                   backendDifferences=backend_differences, backendNormalization="XAudio2 canonicalizes negative zero volume to positive zero",
                   originalOutputSha256=hashlib.sha256(expected).hexdigest(),
                   requestedOutputSha256=hashlib.sha256(requested).hexdigest(),
                   realXAudio2OutputSha256=hashlib.sha256(backend).hexdigest(),
                   validation="original numeric instructions versus actual XAudio2 voice queries; no audible/gameplay/performance claim")
    args.output.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps(receipt))
    assert not mismatches


if __name__ == "__main__":
    main()
