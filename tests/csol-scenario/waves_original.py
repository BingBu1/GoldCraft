"""Original MP instruction oracle for ordinary row clocks and partial reset.

This executes the copied x86 instruction windows in Unicorn, never loads a DLL
natively. Spawn/event dispatch is observed then skipped; its side effects are
outside the row clock. Native --oracle protocol is C/R as agreed with tests.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_MXCSR

EXPECTED_MP = "39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2"
ROW = 0x20000000
GROUP = 0x20001000
FRAME = 0x21008000


def i32(value):
    return struct.unpack("<i", struct.pack("<I", value & 0xFFFFFFFF))[0]


def fbits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def fvalue(bits):
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def evidence(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


class Original:
    def __init__(self, binary, metadata):
        self.data = Path(binary).read_bytes()
        assert hashlib.sha256(self.data).hexdigest() == EXPECTED_MP
        self.meta = json.loads(Path(metadata).read_text(encoding="utf-8-sig"))
        assert self.meta["sha256"] == EXPECTED_MP
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(0x113A4000, 0x3000)
        self.u.mem_write(0x113A4000, self.original(0x113A4000, 0x3000))
        self.u.mem_map(ROW, 0x10000)
        self.u.mem_map(0x21000000, 0x10000)
        self.u.mem_map(0x1222A000, 0x1000)
        # Skip only the debug watchdog/logging work in the overdue advance loop.
        self.u.mem_write(0x1222AC84, b"\x01")
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.mode = None
        self.action = None
        self.stopped_at = None
        for address in (0x113A5865, 0x113A5F12, 0x113A5F4D, 0x113A49F5):
            self.u.hook_add(UC_HOOK_CODE, self.boundary, begin=address, end=address)

    def original(self, address, count):
        rva = address - int(self.meta["image_base"], 16)
        for section in self.meta["sections"]:
            if section["rva"] <= rva and rva + count <= section["rva"] + section["raw_size"]:
                offset = section["raw_offset"] + rva - section["rva"]
                return self.data[offset:offset + count]
        raise ValueError(hex(address))

    def put32(self, address, value):
        self.u.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def get32(self, address):
        return struct.unpack("<i", self.u.mem_read(address, 4))[0]

    def prepare(self, next_time, end, interval):
        self.u.mem_write(ROW, bytes(0x60))
        self.put32(ROW, next_time)
        self.put32(ROW + 4, end)
        self.u.mem_write(ROW + 8, struct.pack("<h", interval))
        self.put32(FRAME - 0x42C, 1)
        self.put32(FRAME - 0x418, GROUP)
        self.u.reg_write(UC_X86_REG_EBP, FRAME)
        self.u.reg_write(UC_X86_REG_ESP, FRAME - 0x500)
        self.u.reg_write(UC_X86_REG_EBX, ROW)
        self.u.reg_write(UC_X86_REG_ESI, GROUP)
        for register in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EDI):
            self.u.reg_write(register, 0)
        self.action = None
        self.stopped_at = None

    def boundary(self, u, address, size, unused):
        if self.mode == "clock":
            if address == 0x113A5865:
                self.action = 2
                # The original increments next before dispatch, then reaches
                # the common post-dispatch end-time comparison at 0x113a5879.
                u.reg_write(UC_X86_REG_EIP, 0x113A5879)
            elif address in (0x113A5F12, 0x113A5F4D):
                if address == 0x113A5F4D:
                    self.action = 0
                elif self.action is None:
                    self.action = 1
                self.stopped_at = address
                u.emu_stop()
        elif self.mode == "reset" and address == 0x113A49F5:
            self.stopped_at = address
            u.emu_stop()

    def clock(self, next_time, end, interval, elapsed_bits):
        self.prepare(next_time, end, interval)
        self.put32(FRAME + 0xC, elapsed_bits)
        self.mode = "clock"
        self.u.emu_start(0x113A581B, 0, count=100000)
        assert self.stopped_at in (0x113A5F12, 0x113A5F4D), (
            f"clock boundary missing: eip={self.u.reg_read(UC_X86_REG_EIP):#x}")
        return [self.action, self.get32(ROW)]

    def reset(self, next_time, end, interval, original_start, original_end, original_interval):
        self.prepare(next_time, end, interval)
        self.put32(ROW + 0x4C, original_start)
        self.put32(ROW + 0x50, original_end)
        self.u.mem_write(ROW + 0x54, struct.pack("<h", original_interval))
        self.u.reg_write(UC_X86_REG_ESI, ROW)
        self.mode = "reset"
        self.u.emu_start(0x113A49D6, 0, count=40)
        assert self.stopped_at == 0x113A49F5
        return [self.get32(ROW), self.get32(ROW + 4), struct.unpack("<h", self.u.mem_read(ROW + 8, 2))[0]]


def cases():
    clocks = []
    for next_time in [-33554433, -16777217, -16777216, -170, -10, -2,
            0, 1, 5, 169, 170, 171, 16777215, 16777216, 16777217, 33554431]:
        elapsed = {fbits(float(next_time)), fbits(float(next_time + 1)), fbits(float(next_time + 5))}
        for center in (fbits(float(next_time)), fbits(float(next_time + 1))):
            if center:
                elapsed.add(center - 1)
            elapsed.add(center + 1)
        for interval in (0, 1, 5, 60, 32767):
            for end in (-170, -2, 0, 1, 170, 33554432):
                clocks.extend((next_time, end, interval, bits) for bits in sorted(elapsed))
    for elapsed in (-170, -2, -0.5, 0, 0.5, 1, 5, 170, 1000):
        for interval in (0, 1, 5):
            clocks.append((-1, 170, interval, fbits(elapsed)))
    # Directly isolate overdue exact-end behavior and early end-before-due.
    clocks.extend([(0, 5, 5, fbits(5)), (10, 5, 5, fbits(6)), (170, 170, 5, fbits(170)),
        (0, 0, 1, fbits(1000)), (0, 170, 5, fbits(170))])
    # These finite branches exercise real LEA/add wrap without entering an
    # unbounded overdue catch-up loop. INT_MAX+1 wraps before cvtdq2ps; its
    # overdue branch then rechecks elapsed <= float32(next) and leaves next
    # unchanged for a nonzero interval. INT_MAX-1 remains due and may wrap on
    # the interval addition. Do not include the next float above 2^31: an
    # overdue positive-interval loop cannot reach that value without overflow.
    for next_time in (-2147483648, -2147483647, 2147483646, 2147483647):
        for interval in (0, 1, 5, 32767):
            for end in (0, -2147483648, 2147483647):
                clocks.append((next_time, end, interval, fbits(float(next_time))))
    resets = [(next_time, end, interval, start, original_end, original_interval)
        for next_time in (-2147483648, -10, -1, 0, 5, 170, 2147483647)
        for end, interval in ((-170, 5), (0, 0), (170, 5))
        for start, original_end, original_interval in ((-10, -2, 5), (0, 170, 5), (2, 0, 0), (17, 99, 60))]
    return clocks, resets


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mp", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    original = Original(args.mp, args.metadata)
    clocks, resets = cases()
    commands = ["C " + " ".join(map(str, case)) for case in clocks]
    commands.extend("R " + " ".join(map(str, case)) for case in resets)
    expected = [original.clock(*case) for case in clocks] + [original.reset(*case) for case in resets]
    native = subprocess.run([str(args.exe.resolve()), "--oracle"], input="\n".join(commands) + "\n",
        capture_output=True, text=True, timeout=30)
    actual = [list(map(int, line.split())) for line in native.stdout.splitlines()]
    failures = [{"command": command, "original": want, "production": got}
        for command, want, got in zip(commands, expected, actual) if want != got]
    complete = native.returncode == 0 and len(actual) == len(expected)
    root = Path(__file__).resolve().parents[2]
    inputs = [args.mp, args.metadata, args.exe, Path(__file__), Path(__file__).with_name("waves_tests.cpp")]
    inputs.extend(sorted((root / "native/csol/scenario").glob("waves.*")))
    result = {"level": "original x86 row-clock/reset instructions versus linked production data layer",
        "mpSHA256": EXPECTED_MP, "nativeDLLLoaded": False, "liveGameTest": False,
        "scope": "finite elapsed including negatives, signed int32 next/end (-1 next is disabled), nonnegative int16 intervals; includes finite LEA/due-add wrapping branches, excludes overdue loops that must overflow int32",
        "mxcsr": "0x1f80", "substitutions": ["spawn/event dispatch observed then skipped", "debug watchdog disabled"],
        "clockCases": len(clocks), "resetCases": len(resets), "complete": complete,
        "returnCode": native.returncode, "stderr": native.stderr, "failures": failures,
        "inputs": [evidence(path) for path in inputs],
        "windows": [{"address": hex(start), "bytes": size,
            "sha256": hashlib.sha256(original.original(start, size)).hexdigest()}
            for start, size in [(0x113A581B, 0x89), (0x113A5E03, 0x14F), (0x113A49D6, 0x1F)]]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"clockCases": len(clocks), "resetCases": len(resets), "complete": complete, "failures": len(failures)}))
    return 0 if complete and not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
