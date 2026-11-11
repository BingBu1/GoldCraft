#!/usr/bin/env python3
"""Execute the original SHA-pinned Studio5006 and shell helper x86 instructions.

Only the real engine model lookup/temporary entity endpoints are stubbed. No
original binary bytes are distributed. SDK callback tests independently check
the fields and pointers handed to the real host API slots.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_ESP

spec = importlib.util.spec_from_file_location("cof_original", Path(__file__).parents[1] / "cryoffear-aim/oracle.py")
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


class Original:
    def __init__(self, sample):
        self.base = shared.Original(sample)
        self.uc = uc = self.base.uc
        uc.mem_write(0x101B2758, shared.u32(shared.CVARS + 0x100))
        uc.mem_write(0x101B2754, shared.u32(shared.CVARS + 0x200))
        uc.mem_write(shared.CVARS + 0x10C, shared.u32(shared.STUB + 0x40))
        uc.mem_write(shared.CVARS + 0x2C0, shared.u32(shared.STUB + 0x60))
        uc.mem_write(shared.STUB + 0x40, b"\xB8" + shared.u32(37) + b"\xC3")
        uc.mem_write(shared.STUB + 0x60, b"\xC3")
        uc.hook_add(UC_HOOK_CODE, self.callback, begin=shared.STUB + 0x60, end=shared.STUB + 0x60)
        self.ranges = {f"{a:08x}-{b:08x}": hashlib.sha256(uc.mem_read(a, b-a)).hexdigest()
                       for a, b in ((0x1004CE7D, 0x1004CEFF), (0x1004E000, 0x1004E04A))}

    def callback(self, uc, address, size, user):
        sp = uc.reg_read(UC_X86_REG_ESP)
        origin, velocity, angles, life, model, sound = struct.unpack("<6I", uc.mem_read(sp + 4, 24))
        assert bytes(uc.mem_read(origin, 12)) == self.origin
        assert bytes(uc.mem_read(angles, 12)) == bytes(12)
        assert life == shared.bits(2.5) and model == 37 and sound == 1
        self.output = bytes(uc.mem_read(velocity, 12))

    def run(self, row):
        uc = self.uc
        raw = struct.pack("<6I", *row)
        self.origin, self.output = raw[:12], None
        uc.mem_write(shared.HUD + 0xB6C, raw)
        uc.reg_write(UC_X86_REG_EDI, shared.HUD)
        uc.reg_write(UC_X86_REG_EBP, shared.STACK + 0x1000)
        uc.reg_write(UC_X86_REG_ESP, shared.STACK + 0x1800)
        uc.emu_start(0x1004CE7D, 0x1004CEFF, count=100)
        assert self.output is not None
        return self.output


def cases():
    yield tuple(map(shared.bits, (1, -2, 3, 4, -6, 8)))
    specials = (0, 0x80000000, 1, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF,
                0x7F800000, 0xFF800000, 0x7FC00001, 0xFFC01234, 0x7F800001)
    for a in specials:
        for b in specials:
            yield a, a, a, b, b, b
    rng = random.Random(0x5006C0F)
    for _ in range(3000):
        yield tuple(rng.getrandbits(32) for _ in range(6))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    original = Original(args.sample)
    rows = list(cases())
    expected = b"".join(original.run(row) for row in rows)
    process = subprocess.run([str(args.runner), "--differential"],
                             input=b"".join(struct.pack("<6I", *row) for row in rows), capture_output=True, check=True)
    actual = process.stdout
    native_original = subprocess.run([str(args.runner), "--differential", "--original-sse"],
                                    input=b"".join(struct.pack("<6I", *row) for row in rows), capture_output=True, check=True).stdout
    assert len(actual) == len(expected)
    mismatches = [dict(case=i, input=[hex(x) for x in row],
                       original=expected[i*12:(i+1)*12].hex(), native=actual[i*12:(i+1)*12].hex())
                  for i, row in enumerate(rows) if expected[i*12:(i+1)*12] != actual[i*12:(i+1)*12]]
    native_mismatches = [i for i in range(len(rows))
                         if actual[i*12:(i+1)*12] != native_original[i*12:(i+1)*12]]
    report = dict(originalSha256=shared.SHA, ranges=original.ranges, cases=len(rows), wordsPerCase=3,
                  emulatorDifferences=mismatches, nativeSseMismatches=native_mismatches,
                  outputSha256=hashlib.sha256(actual).hexdigest(),
                  nativeOriginalSseSha256=hashlib.sha256(native_original).hexdigest(),
                  nativeOriginalSseMatches=actual == native_original,
                  gameplay=False, performance=False)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    assert actual == native_original, "Native original scalar SSE differs from service"
    # SoftFloat/Unicorn can select another NaN payload when both SUBSS operands
    # are NaN. Require the original scalar SSE comparison for every case, and
    # allow emulator divergence only in a lane whose two inputs are both NaN.
    def nan(word):
        return word & 0x7F800000 == 0x7F800000 and word & 0x7FFFFF != 0
    for mismatch in mismatches:
        index = mismatch["case"]
        row = rows[index]
        old = struct.unpack("<3I", expected[index*12:(index+1)*12])
        new = struct.unpack("<3I", actual[index*12:(index+1)*12])
        assert all(a == b or (nan(row[lane]) and nan(row[lane+3]))
                   for lane, (a,b) in enumerate(zip(old,new))), "Unexplained emulator divergence"
    print(f"PASS Studio5006 {len(rows)}cases x3words; native original SSE mismatches0; "
          f"retained emulator dual-NaN differences{len(mismatches)}")


if __name__ == "__main__":
    main()
