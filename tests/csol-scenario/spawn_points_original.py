"""Compare production PointKey/quota against bounded original x86 instructions.

Only the original hash-container lookup is replaced by a deterministic stub.
Signed extensions, branches, comparison, counter increment and return values
execute from the supplied MP bytes in Unicorn. No native DLL load or game I/O.
"""
from pathlib import Path
import argparse
import hashlib
import json
import random
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP

EXPECTED_MP = "39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2"
CODE = 0x1139A000
OBJECT = 0x20000000
NODE = 0x20001000
SENTINEL = 0x20002000
STACK = 0x21008000
STOP = 0x22000000


def signed(value):
    return struct.unpack("<i", struct.pack("<I", value & 0xFFFFFFFF))[0]


def evidence(path):
    data = Path(path).read_bytes()
    return {"path": str(Path(path).resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


class Original:
    def __init__(self, path, metadata):
        self.data = Path(path).read_bytes()
        assert hashlib.sha256(self.data).hexdigest() == EXPECTED_MP
        self.meta = json.loads(Path(metadata).read_text(encoding="utf-8-sig"))
        assert self.meta["sha256"] == EXPECTED_MP and int(self.meta["image_base"], 16) == 0x10000000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(CODE, 0x2000)
        self.u.mem_write(CODE, self.original(CODE, 0x2000))
        self.u.mem_map(OBJECT, 0x10000)
        self.u.mem_map(0x21000000, 0x10000)
        self.u.mem_map(STOP, 0x1000)
        self.present_key = None
        self.lookups = []
        self.stop_at = None
        self.stopped_at = None
        self.u.hook_add(UC_HOOK_CODE, self.lookup, begin=0x1139BED0, end=0x1139BED0)
        # Key sampling and complete quota calls share a start address but have
        # different endpoints. Explicit code hooks also work when Unicorn has
        # already translated a larger block for an earlier complete quota call.
        self.u.hook_add(UC_HOOK_CODE, self.stop_boundary, begin=0x1139AEB6, end=0x1139AEB6)
        self.u.hook_add(UC_HOOK_CODE, self.stop_boundary, begin=STOP, end=STOP)

    def original(self, address, size):
        rva = address - 0x10000000
        for section in self.meta["sections"]:
            if section["rva"] <= rva and rva + size <= section["rva"] + section["raw_size"]:
                offset = section["raw_offset"] + rva - section["rva"]
                return self.data[offset:offset + size]
        raise ValueError(hex(address))

    def get(self, address):
        return struct.unpack("<I", self.u.mem_read(address, 4))[0]

    def put(self, address, value):
        self.u.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def setup(self, team, point_id):
        for register in (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EDI):
            self.u.reg_write(register, 0)
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.u.reg_write(UC_X86_REG_ECX, OBJECT)
        self.u.mem_write(STACK, struct.pack("<III", STOP, team & 0xFFFFFFFF, point_id & 0xFFFFFFFF))
        self.put(OBJECT + 0x2C, SENTINEL)
        self.lookups = []
        self.stopped_at = None

    def stop_boundary(self, u, address, size, unused):
        if address == self.stop_at:
            self.stopped_at = address
            u.emu_stop()

    def run_to(self, end, limit):
        self.stop_at = end
        self.u.emu_start(0x1139AEA0, 0, count=limit)
        eip = self.u.reg_read(UC_X86_REG_EIP)
        assert self.stopped_at == end and eip == end, (
            f"original execution boundary: expected={end:#x} eip={eip:#x} "
            f"hook={self.stopped_at!r} limit={limit}")

    def key(self, team, point_id):
        self.setup(team, point_id)
        self.run_to(0x1139AEB6, 40)
        return signed(self.u.reg_read(UC_X86_REG_EDI))

    def lookup(self, u, address, size, unused):
        esp = u.reg_read(UC_X86_REG_ESP)
        destination = self.get(esp + 4)
        key = signed(self.get(self.get(esp + 8)))
        self.lookups.append(key)
        self.put(destination, NODE if key == self.present_key else SENTINEL)
        u.reg_write(UC_X86_REG_EAX, destination)
        u.reg_write(UC_X86_REG_ESP, esp + 12)
        u.reg_write(UC_X86_REG_EIP, self.get(esp))

    def quota(self, team, point_id, maximum, consumed, exists):
        expected_key = self.key(team, point_id)
        self.setup(team, point_id)
        self.present_key = expected_key if exists else None
        self.put(NODE + 0x28, maximum)
        self.put(NODE + 0x2C, consumed)
        self.run_to(STOP, 250)
        assert self.lookups and all(key == expected_key for key in self.lookups)
        return [int(bool(self.u.reg_read(UC_X86_REG_EAX) & 0xFF)), self.get(NODE + 0x2C), int(bool(exists))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mp", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    original = Original(args.mp, args.metadata)
    boundaries = [-2147483648, -65537, -65536, -32769, -32768, -257, -256, -129, -128, -1,
                  0, 1, 127, 128, 255, 256, 32767, 32768, 65535, 65536, 2147483647]
    keys = [(team, point_id) for team in boundaries for point_id in boundaries]
    rng = random.Random(0x1139AEA0)
    keys.extend((rng.randint(-2147483648, 2147483647), rng.randint(-2147483648, 2147483647)) for _ in range(512))
    maxima = [-2147483648, -2, -1, 0, 1, 2, 2147483647]
    counts = [0, 1, 2, 2147483646, 2147483647, 2147483648, 4294967295]
    quotas = [(2, 7, maximum, count, exists) for maximum in maxima for count in counts for exists in (0, 1)]
    quotas.extend([(1, -1, 2, 0, 1), (-128, 32768, -1, 4294967295, 1), (257, 65543, 1, 4294967295, 1)])
    commands = [f"K {team} {point_id}" for team, point_id in keys]
    commands.extend(f"Q {team} {point_id} {maximum} {count} {exists}" for team, point_id, maximum, count, exists in quotas)
    expected = [[original.key(*case)] for case in keys] + [original.quota(*case) for case in quotas]
    result = subprocess.run([str(args.exe.resolve()), "--oracle"], input="\n".join(commands) + "\n",
        capture_output=True, text=True, timeout=30)
    actual = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    failures = [{"command": command, "original": want, "production": got}
        for command, want, got in zip(commands, expected, actual) if want != got]
    complete = result.returncode == 0 and len(actual) == len(expected)
    root = Path(__file__).resolve().parents[2]
    inputs = [args.mp, args.metadata, args.exe, Path(__file__),
        Path(__file__).with_name("spawn_points_tests.cpp"),
        root / "native/csol/scenario/spawn_points.hpp", root / "native/csol/scenario/spawn_points.cpp"]
    output = {"evidenceLevel": "original x86 instructions versus unchanged production data-layer executable",
        "nativeDLLLoaded": False, "liveGameTest": False,
        "substitutedService": "hash-container lookup only; signed key and quota instructions are original",
        "originalCode": {"keyEntry": "0x1139aea0", "keyEnd": "0x1139aeb6", "quotaEntry": "0x1139aea0",
            "boundaryControl": "explicit code hooks, confirmed EIP and hook arrival",
            "lookupStub": "0x1139bed0", "quotaBytesSHA256": hashlib.sha256(original.original(0x1139AEA0, 0xB6)).hexdigest()},
        "inputs": [evidence(path) for path in inputs],
        "keyCases": len(keys), "quotaCases": len(quotas), "complete": complete,
        "returnCode": result.returncode, "stderr": result.stderr, "failures": failures}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"keyCases": len(keys), "quotaCases": len(quotas), "complete": complete, "failures": len(failures)}))
    return 0 if complete and not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
