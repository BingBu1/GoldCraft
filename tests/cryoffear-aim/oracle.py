#!/usr/bin/env python3
"""Replay SHA-pinned original x86 FOV instructions; compare native float bits.

No original module/bytes are distributed. Provide the private client.dll and a
built runner. Only engine time/cvar providers are stubbed, using x87 loads.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess

import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (
    UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP,
    UC_X86_REG_FPCW, UC_X86_REG_MXCSR,
)

SHA = "d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00"
BASE, HUD, CVARS, STUB, STACK = 0x10000000, 0x20000000, 0x21000000, 0x30000000, 0x40000000
STOP, DATA = STUB + 0x100, STUB + 0x1000
INPUT = struct.Struct("<3fii6f")
OUTPUT = struct.Struct("<6I")
REGRESSIONS = {
    "initial_negative": (-1.0, 10.0, 90.0, 2, 90, 12.25, 0.0, 100.0, 2.5, 1.2, 1.0),
    "mode666_requires_custom": (80.0, 10.0, 30.0, 666, 90, 10.0, 0.0, 100.0, 2.5, 1.2, 1.0),
    "mode666_preserves_cache": (80.0, 10.0, 30.0, 666, 90, 10.0, 50.0, 100.0, 2.5, 1.2, 1.0),
    "speed666_preserves_cache": (80.0, 10.0, 30.0, 45, 90, 10.0, 666.0, 100.0, 2.5, 1.2, 1.0),
    "scope_ignores_custom666": (80.0, 10.0, 30.0, 3, 90, 10.0, 666.0, 100.0, 2.5, 1.2, 1.0),
    "exact_up_keeps_cache": (40.0, 0.0, 90.0, 45, 90, 5.0, 1.0, 100.0, 2.5, 1.2, 1.0),
    "overshoot_up_sets_cache": (40.0, 0.0, 90.0, 45, 90, 6.0, 1.0, 100.0, 2.5, 1.2, 1.0),
    "exact_down_keeps_cache": (50.0, 0.0, 90.0, 45, 90, 5.0, 1.0, 100.0, 2.5, 1.2, 1.0),
    "overshoot_down_sets_cache": (50.0, 0.0, 90.0, 45, 90, 6.0, 1.0, 100.0, 2.5, 1.2, 1.0),
    "truncated_default_sensitivity_zero": (90.75, 10.0, 60.0, 90, 90, 10.0, 0.0, 100.0, 2.5, 1.2, 1.0),
}


def u32(value):
    return struct.pack("<I", value & 0xFFFFFFFF)


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


class Original:
    def __init__(self, path):
        image = Path(path).read_bytes()
        assert hashlib.sha256(image).hexdigest() == SHA, "Original identity mismatch"
        pe = struct.unpack_from("<I", image, 0x3C)[0]
        assert image[pe:pe + 4] == b"PE\0\0"
        assert struct.unpack_from("<H", image, pe + 4)[0] == 0x14C
        count, optional_size = struct.unpack_from("<H12xH", image, pe + 6)
        optional = pe + 24
        assert struct.unpack_from("<I", image, optional + 28)[0] == BASE
        size = struct.unpack_from("<I", image, optional + 56)[0]
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(BASE, (size + 0xFFF) & ~0xFFF)
        sections = optional + optional_size
        self.sections = []
        for index in range(count):
            row = sections + index * 40
            rva, raw_size, raw = struct.unpack_from("<III", image, row + 12)
            uc.mem_write(BASE + rva, image[raw:raw + raw_size])
            self.sections.append((rva, raw_size, raw))
        for address, length in ((HUD, 0x4000), (CVARS, 0x1000), (STUB, 0x2000), (STACK, 0x3000)):
            uc.mem_map(address, length)
        # GetClientTime and GetCvarFloat both return float through x87.
        uc.mem_write(STUB, b"\xD9\x05" + u32(DATA) + b"\xC3")
        uc.mem_write(STUB + 0x20, b"\xD9\x05" + u32(DATA + 8) + b"\xC3")
        uc.mem_write(0x101B26E0, u32(STUB))
        uc.mem_write(0x101B2644, u32(STUB + 0x20))
        uc.mem_write(0x1054355C, u32(CVARS))
        uc.mem_write(0x105433B0, u32(CVARS + 0x40))
        uc.mem_write(HUD + 0x313C, u32(CVARS + 0x20))
        uc.reg_write(UC_X86_REG_FPCW, 0x37F)
        uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.ranges = {}
        for start, end in ((0x10073824, 0x10073A43), (0x100780C9, 0x100780E0)):
            self.ranges[f"{start:08x}-{end:08x}"] = hashlib.sha256(uc.mem_read(start, end - start)).hexdigest()

    def run(self, row):
        current, previous, settled, mode, default, time, custom, speed, sensitivity, ratio, mult = row
        uc = self.uc
        for address, value in ((HUD + 0x3124, current), (0x1054321C, previous),
                               (0x10543220, settled), (HUD + 0x3144, custom),
                               (CVARS + 12, sensitivity), (CVARS + 0x2C, speed),
                               (CVARS + 0x4C, mult), (DATA + 8, ratio)):
            uc.mem_write(address, struct.pack("<f", value))
        uc.mem_write(0x105431D8, u32(mode))
        uc.mem_write(0x1018FEC0, u32(default))
        uc.mem_write(DATA, struct.pack("<f", time))
        bp = STACK + 0x1000
        uc.mem_write(bp - 20, bytes(20) + u32(0) + u32(STOP))
        uc.reg_write(UC_X86_REG_EBP, bp)
        uc.reg_write(UC_X86_REG_ESP, bp - 20)
        uc.reg_write(UC_X86_REG_EDI, HUD)
        uc.emu_start(0x10073824, STOP, count=2000)
        uc.reg_write(UC_X86_REG_EDI, HUD)
        uc.reg_write(UC_X86_REG_ESI, HUD + 0x3500)
        uc.emu_start(0x100780C9, 0x100780E0, count=20)
        return b"".join(uc.mem_read(address, 4) for address in
                        (HUD + 0x3124, 0x1054321C, 0x10543220,
                         HUD + 0x3120, HUD + 0x14, HUD + 0x351C))


def cases():
    yield from REGRESSIONS.values()
    # Cross-product covers both directions, initialization, exact target,
    # mode666 with/without custom speed, and elapsed-time rounding/boundaries.
    for mode, current, custom, elapsed in itertools.product(
            (-7, 0, 1, 2, 3, 4, 45, 90, 666),
            (-1.0, -0.0, 30.0, 60.0, 90.0, 130.0),
            (0.0, 50.0, 666.0), (-0.02, 0.0, 0.001, 0.3, 5.0)):
        yield (current, 10.0, 30.0, mode, 90, 10.0 + elapsed, custom, 100.0, 2.5, 1.2, 1.0)
    rng = random.Random(0xC0F)
    for _ in range(4000):
        yield (rng.uniform(-5, 200), rng.uniform(0, 100), rng.choice((30.0, 60.0, 90.0)),
               rng.choice((0, 2, 3, 4, 45, 90, 666, 2147483647, -2147483648)),
               rng.choice((0, 75, 90, 110, 2147483647)), rng.uniform(0, 120),
               rng.choice((0.0, -10.0, 50.0, 666.0)), rng.uniform(0, 300),
               rng.uniform(0, 5), rng.uniform(0, 2), rng.uniform(0.5, 2))
    base = [80.0, 10.0, 60.0, 45, 90, 10.01, 50.0, 100.0, 2.5, 1.2, 1.0]
    # Special values exercise ordered comparisons, CVTTSS2SI, MAXSS and sign.
    for position in (0, 1, 2, 6, 7, 8, 9, 10):
        for value in (float("inf"), -float("inf"), float("nan"), -0.0, 1e-40):
            row = base.copy()
            row[position] = value
            yield tuple(row)
    for time in (float("inf"), -float("inf"), float("nan"), 16777216.25, 16777217.25):
        row = base.copy()
        row[5] = time
        yield tuple(row)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    original = Original(args.sample)
    rows = list(cases())
    trajectories = []
    for label, modes in (("scope_enter_hold_exit", [3] * 80 + [0] * 100),
                         ("numeric_enter_speed666_exit", [50] * 60 + [90] * 100)):
        state = (90.0, 20.0, 90.0)
        for index, mode in enumerate(modes):
            row = (*state, mode, 90, 20.0 + (index + 1) * 0.016,
                   666.0 if label.startswith("numeric") and index == 60 else 50.0,
                   100.0, 2.5, 1.2, 1.1)
            encoded = INPUT.pack(*row)
            row = INPUT.unpack(encoded)
            result = original.run(row)
            state = struct.unpack("<3f", result[:12])
            rows.append(row)
        trajectories.append(label)
    # Encode/decode first: native runner and original receive identical f32 bits.
    payload = b"".join(INPUT.pack(*row) for row in rows)
    rows = list(INPUT.iter_unpack(payload))
    expected = b"".join(original.run(row) for row in rows)
    named = dict(zip(REGRESSIONS, OUTPUT.iter_unpack(expected)))
    assert named["initial_negative"][0] == named["initial_negative"][2] == bits(60.0)
    assert named["mode666_requires_custom"][0] == bits(80.0)
    assert named["mode666_preserves_cache"][0] == bits(666.0)
    assert named["speed666_preserves_cache"][0] == bits(45.0)
    assert named["scope_ignores_custom666"][0] == bits(80.0)
    for name in ("mode666_preserves_cache", "speed666_preserves_cache"):
        assert named[name][2] == bits(30.0)
    for direction in ("up", "down"):
        assert named[f"exact_{direction}_keeps_cache"][0] == bits(45.0)
        assert named[f"exact_{direction}_keeps_cache"][2] == bits(90.0)
        assert named[f"overshoot_{direction}_sets_cache"][2] == bits(45.0)
    assert named["truncated_default_sensitivity_zero"][4] == bits(0.0)
    actual = subprocess.run([str(args.runner.resolve())], input=payload, capture_output=True, check=True).stdout
    assert len(actual) == len(expected), (len(actual), len(expected))
    failures = []
    for index, (want, got) in enumerate(zip(OUTPUT.iter_unpack(expected), OUTPUT.iter_unpack(actual))):
        if want != got:
            failures.append(dict(case=index, input=[repr(v) for v in rows[index]],
                                 original=[f"{v:08x}" for v in want], native=[f"{v:08x}" for v in got]))
    receipt = dict(sampleSha256=SHA, unicornVersion=unicorn.__version__, originalRanges=original.ranges,
                   cases=len(rows), outputWordsPerCase=6, mismatchCount=len(failures),
                   mismatches=failures[:20], inputSha256=hashlib.sha256(payload).hexdigest(),
                   originalOutputSha256=hashlib.sha256(expected).hexdigest(),
                   nativeOutputSha256=hashlib.sha256(actual).hexdigest(),
                   namedRegressions=list(REGRESSIONS),
                   trajectories=trajectories,
                   validation="original x86 instruction differential replay; no live host/gameplay/performance claim")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(json.dumps({key: receipt[key] for key in ("cases", "mismatchCount", "originalOutputSha256", "nativeOutputSha256")}))
    assert not failures, failures[:3]


if __name__ == "__main__":
    main()
