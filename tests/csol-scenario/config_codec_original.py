"""Original copied HW configuration TEA versus the public raw C++ decoder.

Only aligned buffers are passed to the original function. Production rejection
of incomplete blocks is a separate safety check, not claimed original behavior.
The original function has no magic/version/padding checks and no status return.
"""
from pathlib import Path
import argparse
import hashlib
import json
import random
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

EXPECTED_HW = "c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783"
BUFFER = 0x10000000
STACK = 0x20008000
STOP = 0x107FF000
KEY = (0xB38520FD, 0x5F30E75F, 0xE2EB67E0, 0xFC67A521)
DELTA = 0x9E3779B9
MASK = 0xFFFFFFFF


def evidence(path):
    path = Path(path).resolve()
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def encrypt_fixture(plain):
    """Generate plaintext fixtures; expected decode still executes original HW."""
    assert len(plain) % 8 == 0
    output = bytearray()
    for at in range(0, len(plain), 8):
        left, right = struct.unpack_from("<II", plain, at)
        total = 0
        for unused in range(32):
            total = (total + DELTA) & MASK
            left = (left + ((((right << 4) + KEY[0]) & MASK) ^
                ((right + total) & MASK) ^ ((right >> 5) + KEY[1]))) & MASK
            right = (right + ((((left << 4) + KEY[2]) & MASK) ^
                ((left + total) & MASK) ^ ((left >> 5) + KEY[3]))) & MASK
        output.extend(struct.pack("<II", left, right))
    return bytes(output)


class Original:
    def __init__(self, binary, metadata, capacity):
        self.data = Path(binary).read_bytes()
        assert hashlib.sha256(self.data).hexdigest() == EXPECTED_HW
        self.meta = json.loads(Path(metadata).read_text(encoding="utf-8-sig"))
        assert self.meta["sha256"] == EXPECTED_HW
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        for address in (0x37C18000, 0x3833A000):
            self.u.mem_map(address, 0x1000)
            self.u.mem_write(address, self.original(address, 0x1000))
        assert struct.unpack("<I", self.original(0x3833A720, 4))[0] == DELTA
        self.u.mem_map(BUFFER, (capacity + 31 + 0xFFF) & ~0xFFF)
        self.u.mem_map(0x20000000, 0x10000)
        self.u.mem_map(STOP, 0x1000)
        self.stopped = False
        self.u.hook_add(UC_HOOK_CODE, self.boundary, begin=STOP, end=STOP)

    def original(self, address, count):
        rva = address - int(self.meta["image_base"], 16)
        for section in self.meta["sections"]:
            if section["rva"] <= rva and rva + count <= section["rva"] + section["raw_size"]:
                offset = section["raw_offset"] + rva - section["rva"]
                return self.data[offset:offset + count]
        raise ValueError(hex(address))

    def boundary(self, u, address, size, unused):
        self.stopped = True
        u.emu_stop()

    def decode(self, encoded):
        assert len(encoded) % 8 == 0
        guard = bytes.fromhex("0123456789abcdef") * 2
        self.u.mem_write(BUFFER, encoded + guard)
        self.u.mem_write(STACK, struct.pack("<III", STOP, BUFFER, len(encoded)))
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.stopped = False
        self.u.emu_start(0x37C18460, 0, count=max(10000, (len(encoded) // 8) * 1500 + 1000))
        assert self.stopped and self.u.reg_read(UC_X86_REG_EIP) == STOP
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK + 12
        assert bytes(self.u.mem_read(BUFFER + len(encoded), len(guard))) == guard
        return bytes(self.u.mem_read(BUFFER, len(encoded))) if encoded else b""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hw", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--resource", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rng = random.Random(0xC50C0DEC)
    aligned = [{"name": f"cipher-{size}-{sample}",
        "encoded": bytes(rng.randrange(256) for unused in range(size))}
        for size in (0, 8, 16, 24, 32, 64, 128, 256) for sample in range(4)]
    plaintexts = [b"", bytes(8), bytes(16), b"12345678", bytes(range(256)),
        b"waveID,spawnPoint,startTime\r\r\n1,2,0\r\r\n", b"A\0B\0C", b"no-nul!!"]
    for index, plain in enumerate(plaintexts):
        padded = plain + bytes((-len(plain)) % 8)
        aligned.append({"name": f"known-plain-{index}", "encoded": encrypt_fixture(padded),
            "knownPlain": padded})
    for path in args.resource:
        encoded = path.read_bytes()
        assert len(encoded) > 0 and len(encoded) % 8 == 0
        aligned.append({"name": str(path), "encoded": encoded})
    original = Original(args.hw, args.metadata, max(len(case["encoded"]) for case in aligned))
    commands, expected, records = [], [], []
    for case in aligned:
        encoded = case["encoded"]
        plain = original.decode(encoded)
        if "knownPlain" in case:
            assert plain == case["knownPlain"]
        commands.append("D " + (encoded.hex() or "-"))
        expected.append([1, plain.hex() or "-"])
        records.append({"name": case["name"], "bytes": len(encoded),
            "cipherSHA256": hashlib.sha256(encoded).hexdigest(),
            "rawPlainSHA256": hashlib.sha256(plain).hexdigest(),
            "trimmedPlainSHA256": hashlib.sha256(plain.rstrip(b"\0")).hexdigest(),
            "trailingNuls": len(plain) - len(plain.rstrip(b"\0"))})
    # Do not run these inputs through the original: its ceil(len/8) loop reads
    # and writes a complete final block even when declared size is incomplete.
    malformed = [bytes(range(size)) for size in range(1, 32) if size % 8]
    for encoded in malformed:
        commands.append("D " + encoded.hex())
        expected.append([0, encoded.hex()])
    native = subprocess.run([str(args.exe.resolve()), "--oracle"], input="\n".join(commands) + "\n",
        capture_output=True, text=True, timeout=30)
    actual = []
    for line in native.stdout.splitlines():
        success, hex_bytes = line.split()
        actual.append([int(success), hex_bytes])
    failures = [{"case": index, "kind": "original" if index < len(aligned) else "safety-reject",
        "expected": want, "production": got}
        for index, (want, got) in enumerate(zip(expected, actual)) if want != got]
    complete = native.returncode == 0 and len(actual) == len(expected)
    root = Path(__file__).resolve().parents[2]
    paths = [args.hw, args.metadata, args.exe, Path(__file__),
        root / "native/csol/config_codec.hpp", Path(__file__).with_name("resources_tests.cpp"), *args.resource]
    result = {"level": "unaltered original HW TEA function versus production raw decoder",
        "hwSHA256": EXPECTED_HW, "nativeDLLLoaded": False, "liveGameTest": False,
        "originalFunction": "0x37c18460", "substitutions": [], "formatHeader": False,
        "originalAlignedCases": len(aligned), "knownPlainCases": len(plaintexts),
        "productionSafetyCases": len(malformed), "incompleteOriginalExecuted": False,
        "policyDifferences": ["incomplete final blocks are rejected without mutation",
            "raw API supplies a bool status; original routine has no status contract"],
        "trimNote": "raw decoder preserves every byte; original caller 0x37c164d8..e9 trims trailing NULs before table parsing",
        "complete": complete, "returnCode": native.returncode, "stderr": native.stderr,
        "failures": failures, "records": records, "inputs": [evidence(path) for path in paths],
        "originalWindowSHA256": hashlib.sha256(original.original(0x37C18460, 0xAD)).hexdigest()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"originalAlignedCases": len(aligned), "productionSafetyCases": len(malformed),
        "complete": complete, "failures": len(failures)}))
    return 0 if complete and not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
