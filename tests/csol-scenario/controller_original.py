"""Bounded original MP schedule arithmetic and branch oracle.

The active wave container is covered by native integration fixtures and source
review, not emulated here. This never loads the copied DLL natively. Every
tested original window has an explicit stopping hook; native values use U32
float bits throughout the agreed S/B/F stdin protocol.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EDI,
    UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_MXCSR)

EXPECTED_MP = "39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2"
MANAGER = 0x20000000
FRAME = 0x21008000
ENGINE = 0x22000000
FREEZE = ENGINE + 4
STUB = 0x23000000


def fbits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


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
        for address, count in ((0x11382000, 0x1000), (0x11388000, 0x1000), (0x1178F000, 0x1000)):
            self.u.mem_map(address, count)
            self.u.mem_write(address, self.original(address, count))
        for address in (MANAGER, 0x21000000, ENGINE, STUB):
            self.u.mem_map(address, 0x10000)
        self.u.mem_map(0x12278000, 0x1000)
        self.put32(0x122788F4, ENGINE)
        self.u.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        # Substitute only the engine cvar return: FNINIT; FLD freeze; JMP to
        # original FADD now / FSTP manager.start. The round-origin arithmetic
        # itself remains the copied original x87 instructions.
        entry = b"\xdb\xe3\xd9\x05" + struct.pack("<I", FREEZE)
        entry += b"\xe9" + struct.pack("<i", 0x11382BFA - (STUB + len(entry) + 5))
        self.u.mem_write(STUB, entry)
        self.operation = None
        self.step = None
        self.stopped_at = None
        self.skipped_mode_work = False
        for address in (0x11382C04, 0x113880C8, 0x11388636,
                0x113882D5, 0x113881E6, 0x11388217):
            self.u.hook_add(UC_HOOK_CODE, self.boundary, begin=address, end=address)

    def original(self, address, count):
        rva = address - int(self.meta["image_base"], 16)
        for section in self.meta["sections"]:
            if section["rva"] <= rva and rva + count <= section["rva"] + section["raw_size"]:
                offset = section["raw_offset"] + rva - section["rva"]
                return self.data[offset:offset + count]
        raise ValueError(hex(address))

    def put32(self, address, value):
        self.u.mem_write(address, struct.pack("<I", value))

    def get32(self, address):
        return struct.unpack("<I", self.u.mem_read(address, 4))[0]

    def prepare(self, operation, start, elapsed, next_think):
        self.operation = operation
        self.step = None
        self.stopped_at = None
        self.skipped_mode_work = False
        self.put32(MANAGER + 0x129C, start)
        self.put32(MANAGER + 0x12A0, elapsed)
        self.put32(MANAGER + 0x12B0, next_think)
        self.u.reg_write(UC_X86_REG_EBP, FRAME)
        self.u.reg_write(UC_X86_REG_ESP, FRAME - 0x100)
        self.u.reg_write(UC_X86_REG_ESI, MANAGER)
        self.u.reg_write(UC_X86_REG_EDI, MANAGER)
        self.u.reg_write(UC_X86_REG_EAX, 0)

    def state(self):
        return [self.get32(MANAGER + field) for field in (0x129C, 0x12A0, 0x12B0)]

    def boundary(self, u, address, size, unused):
        if self.operation == "B" and address == 0x11382C04:
            self.step = 1
        elif self.operation == "F" and address == 0x11388217:
            self.step = 1
        elif self.operation == "S":
            if address == 0x113880C8:
                # Leave unrelated mode updates outside the scalar schedule;
                # enter the original globalNow versus nextThink gate.
                self.skipped_mode_work = True
                u.reg_write(UC_X86_REG_EIP, 0x113881D0)
                return
            if address == 0x11388636:
                self.step = 1  # Frozen; prior elapsed remains untouched.
            elif address == 0x113882D5:
                self.step = 2  # Waiting.
            elif address == 0x113881E6:
                self.step = 3  # Dispatch is about to be called.
            else:
                return
        else:
            return
        self.stopped_at = address
        u.emu_stop()

    def run(self, operation, values):
        self.prepare(operation, *values[:3])
        if operation == "B":
            self.put32(FRAME - 0x14, values[3])
            self.put32(FREEZE, values[4])
            start = STUB
            expected_stops = (0x11382C04,)
        elif operation == "S":
            self.put32(FRAME + 8, values[3])
            self.put32(ENGINE, values[4])
            start = 0x113880A2
            expected_stops = (0x11388636, 0x113882D5, 0x113881E6)
        else:
            assert operation == "F"
            self.put32(ENGINE, values[3])
            start = 0x113881FB
            expected_stops = (0x11388217,)
        self.u.emu_start(start, 0, count=100)
        assert self.stopped_at in expected_stops, (
            f"{operation} boundary missing at {self.u.reg_read(UC_X86_REG_EIP):#x}")
        if operation == "S":
            assert self.skipped_mode_work == (self.step != 1)
        return [self.step] + self.state()


def cases():
    result = []
    initial_elapsed = fbits(7.25)
    for origin in (-100.0, -0.0, 0.0, 10.0, 16777216.0):
        for next_think in (-1.0, 0.0, 0.5, 10.0, 16777216.0):
            for think in (origin - 1.0, origin, origin + 0.5, origin + 1.0, origin + 10.0):
                gate = fbits(next_think)
                engine_values = {gate}
                if gate:
                    engine_values.add(gate - 1)
                engine_values.add(gate + 1)
                for engine in sorted(engine_values):
                    result.append(("S", (fbits(origin), initial_elapsed, gate, fbits(think), engine)))
    for old_start in (-10.0, 0.0, 100.0):
        for now in (-100.0, -0.0, 0.0, 1.0, 16777216.0):
            for freeze in (-5.0, -0.0, 0.0, 0.5, 3.0, 5.0):
                result.append(("B", (fbits(old_start), initial_elapsed, fbits(19.5), fbits(now), fbits(freeze))))
    for engine in (-100.0, -0.0, 0.0, 1.0, 1.25, 100.0, 16777215.0, 16777216.0, 33554432.0):
        for old_next in (0.0, 0.5, 200.0):
            result.append(("F", (fbits(10.0), initial_elapsed, fbits(old_next), fbits(engine))))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mp", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    original = Original(args.mp, args.metadata)
    inputs = cases()
    commands = [operation + " " + " ".join(map(str, values)) for operation, values in inputs]
    expected = [original.run(operation, values) for operation, values in inputs]
    native = subprocess.run([str(args.exe.resolve()), "--oracle"], input="\n".join(commands) + "\n",
        capture_output=True, text=True, timeout=30)
    actual = [list(map(int, line.split())) for line in native.stdout.splitlines()]
    failures = [{"command": command, "original": want, "production": got}
        for command, want, got in zip(commands, expected, actual) if want != got]
    complete = native.returncode == 0 and len(actual) == len(expected)
    root = Path(__file__).resolve().parents[2]
    evidence_paths = [args.mp, args.metadata, args.exe, Path(__file__),
        Path(__file__).with_name("controller_tests.cpp")]
    evidence_paths.extend(sorted((root / "native/csol/scenario").glob("schedule.*")))
    result = {"level": "original x86 scalar schedule windows versus linked production schedule",
        "mpSHA256": EXPECTED_MP, "nativeDLLLoaded": False, "liveGameTest": False,
        "scope": "finite scalar inputs; freeze/elapsed, inclusive globalNow gate, post-dispatch fresh now plus 0.5, x87 round origin",
        "notCovered": ["active wave container behavior", "actual dispatch", "mode work", "complete round restart lifecycle"],
        "substitutions": ["engine cvar return supplied as x87 ST0", "mode side effects skipped between elapsed check and gate"],
        "mxcsr": "0x1f80", "x87Initialization": "FNINIT before each Begin case",
        "cases": len(inputs), "operationCounts": {op: sum(one[0] == op for one in inputs) for op in "SBF"},
        "complete": complete, "returnCode": native.returncode, "stderr": native.stderr,
        "failures": failures, "inputs": [evidence(path) for path in evidence_paths],
        "windows": [{"address": hex(start), "bytes": size,
            "sha256": hashlib.sha256(original.original(start, size)).hexdigest()}
            for start, size in [(0x11382BFA, 0xA), (0x113880A2, 0x26),
                (0x113881D0, 0x16), (0x113881FB, 0x1C)]]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"cases": len(inputs), "complete": complete, "failures": len(failures)}))
    return 0 if complete and not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
