"""Execute only the original MP six-comparison trigger-bounds window.

The grid, player filtering, commands and engine are deliberately not executed.
Produces TGEO: magic, LE u32 count, then 10 LE u32 per case (mins bits,
maxs bits, origin bits, expected boolean). No production implementation import.
"""
from pathlib import Path
import argparse
import hashlib
import json
import random
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_EDI, UC_X86_REG_EFLAGS, UC_X86_REG_MXCSR,
    UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3,
)

MP_SHA256 = "39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2"
ENTRY, ACCEPT, REJECT = 0x1139C4A8, 0x1139C4DD, 0x1139C4E5
VOLUME, ENTVARS = 0x20000000, 0x20000100


def evidence(path):
    path = Path(path).resolve()
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


class Original:
    def __init__(self, binary, metadata):
        raw = Path(binary).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == MP_SHA256
        meta = json.loads(Path(metadata).read_text(encoding="utf-8-sig"))
        assert meta["sha256"] == MP_SHA256
        rva = ENTRY - int(meta["image_base"], 16)
        section = next(s for s in meta["sections"] if s["rva"] <= rva and rva + REJECT - ENTRY <= s["rva"] + s["raw_size"])
        offset = section["raw_offset"] + rva - section["rva"]
        self.code = raw[offset:offset + REJECT - ENTRY]
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(ENTRY & ~0xFFF, 0x1000)
        self.u.mem_write(ENTRY, self.code)
        self.u.mem_map(VOLUME, 0x1000)
        self.u.hook_add(UC_HOOK_CODE, self.stop, begin=ACCEPT, end=REJECT)
        self.result = None

    def stop(self, u, address, size, unused):
        if address in (ACCEPT, REJECT):
            self.result = address == ACCEPT
            u.emu_stop()
        else:
            raise AssertionError(f"Unexpected instruction after decision: {address:#x}")

    def contains(self, values):
        mins, maxs, origin = values[:3], values[3:6], values[6:9]
        self.u.mem_write(VOLUME + 4, struct.pack("<6I", *mins, *maxs))
        self.u.mem_write(ENTVARS + 8, struct.pack("<3I", *origin))
        self.u.reg_write(UC_X86_REG_EAX, VOLUME)
        self.u.reg_write(UC_X86_REG_EDI, ENTVARS)
        self.u.reg_write(UC_X86_REG_EFLAGS, 2)
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.u.reg_write(UC_X86_REG_XMM0, 0)
        self.u.reg_write(UC_X86_REG_XMM1, 0)
        self.u.reg_write(UC_X86_REG_XMM2, origin[1])
        self.u.reg_write(UC_X86_REG_XMM3, origin[0])
        self.result = None
        self.u.emu_start(ENTRY, REJECT + 1, count=32)
        assert self.result is not None
        return self.result


def cases():
    base = [bits(-1.0)] * 3 + [bits(1.0)] * 3 + [bits(0.0)] * 3
    yield "center", base
    finite = [-2.0, -1.0, -0.9999999403953552, -0.0, 0.0, 0.9999999403953552, 1.0, 2.0]
    special = [0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7F800001, 0x00000001, 0x80000001]
    for field in range(9):
        for value in [bits(x) for x in finite] + special:
            row = base.copy()
            row[field] = value
            yield f"field{field}-{value:08x}", row
    for axis in range(3):
        for lo, hi in [(0.0, 0.0), (1.0, -1.0), (float("-inf"), float("inf")), (float("inf"), float("inf")), (float("-inf"), float("-inf"))]:
            for point in [float("-inf"), -2.0, -0.0, 0.0, 2.0, float("inf")]:
                row = base.copy()
                row[axis], row[3 + axis], row[6 + axis] = bits(lo), bits(hi), bits(point)
                yield f"axis{axis}-range-{bits(lo):08x}-{bits(hi):08x}-{bits(point):08x}", row
    rng = random.Random(0xC4A8)
    for index in range(128):
        yield f"raw-{index}", [rng.getrandbits(32) for _ in range(9)]
    for index in range(128):
        lo = [rng.uniform(-8192, 4096) for _ in range(3)]
        hi = [value + rng.uniform(1, 4096) for value in lo]
        point = [rng.uniform(a - 512, b + 512) for a, b in zip(lo, hi)]
        yield f"finite-{index}", [bits(v) for v in lo + hi + point]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mp", required=True)
    parser.add_argument("--metadata", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--corpus", required=True)
    args = parser.parse_args()
    original = Original(args.mp, args.metadata)
    rows = [{"case": name, "inputBits": values, "expected": original.contains(values)} for name, values in cases()]
    payload = b"TGEO" + struct.pack("<I", len(rows))
    payload += b"".join(struct.pack("<10I", *row["inputBits"], int(row["expected"])) for row in rows)
    Path(args.corpus).parent.mkdir(parents=True, exist_ok=True)
    Path(args.corpus).write_bytes(payload)
    result = {
        "validationLevel": "Original six COMISS/JBE bounds comparisons under Unicorn only",
        "productionExecuted": False, "nativeDllLoaded": False, "liveGameTest": False,
        "inputs": [evidence(args.mp), evidence(args.metadata), evidence(__file__)],
        "window": {"entry": hex(ENTRY), "accept": hex(ACCEPT), "reject": hex(REJECT), "bytesHex": original.code.hex(), "sha256": hashlib.sha256(original.code).hexdigest()},
        "mxcsr": "0x1f80 (exceptions masked, DAZ/FTZ disabled)",
        "cases": rows, "count": len(rows), "corpus": evidence(args.corpus),
    }
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"originalCases": len(rows), "corpus": result["corpus"], "productionExecuted": False}))


if __name__ == "__main__":
    main()
