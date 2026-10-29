"""Execute copied HW CSV scalar getters with a bounded UCRT atoi import stub.

This does not load either game DLL or run the game. The wrapper, numeric getter,
and string-cell accessor execute unmodified copied x86 instructions. The external
atoi import is supplied by the host UCRT only for non-overflowing ASCII inputs.
The result is evidence for conversion/default/narrowing, not a full CSV-loader or
production-parser acceptance claim.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

HW_SHA256 = "c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783"
OBJECT = 0x10000000
COLUMNS = OBJECT + 0x100
CELL = OBJECT + 0x200
TEXT = OBJECT + 0x300
OUTPUT = OBJECT + 0x500
STACK = 0x20008000
STOP = 0x21000000
ATOI = STOP + 0x100
SENTINEL = 0x13572468


def evidence(path):
    path = Path(path).resolve()
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


class Original:
    def __init__(self, binary, metadata):
        self.data = Path(binary).read_bytes()
        assert hashlib.sha256(self.data).hexdigest() == HW_SHA256
        self.meta = json.loads(Path(metadata).read_text(encoding="utf-8-sig"))
        assert self.meta["sha256"] == HW_SHA256
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        for address in (0x3732D000, 0x37C15000, 0x38013000, 0x3811A000, 0x3800F000):
            self.u.mem_map(address, 0x1000)
            self.u.mem_write(address, self.original(address, 0x1000))
        self.u.mem_map(OBJECT, 0x1000)
        self.u.mem_map(0x20000000, 0x10000)
        self.u.mem_map(STOP, 0x1000)
        # GetCell increments this original diagnostic counter on upper-bound
        # misses before returning nullptr. It is zero-initialized BSS data.
        self.u.mem_map(0x39417000, 0x1000)
        self.u.mem_write(0x3800FD68, struct.pack("<I", ATOI))
        self.crt = ctypes.CDLL("ucrtbase.dll")
        self.crt.atoi.argtypes = [ctypes.c_char_p]
        self.crt.atoi.restype = ctypes.c_int
        self.stopped = False
        self.import_calls = []
        self.u.hook_add(UC_HOOK_CODE, self.callback, begin=STOP, end=ATOI)

    def original(self, address, count):
        rva = address - int(self.meta["image_base"], 16)
        for section in self.meta["sections"]:
            if section["rva"] <= rva and rva + count <= section["rva"] + section["raw_size"]:
                offset = section["raw_offset"] + rva - section["rva"]
                return self.data[offset:offset + count]
        raise ValueError(hex(address))

    def callback(self, u, address, size, unused):
        if address == STOP:
            self.stopped = True
            u.emu_stop()
            return
        assert address == ATOI
        stack = u.reg_read(UC_X86_REG_ESP)
        destination, string_address = struct.unpack("<II", u.mem_read(stack, 8))
        value = bytes(u.mem_read(string_address, 128)).split(b"\0", 1)[0]
        result = self.crt.atoi(value)
        self.import_calls.append({"text": value.decode("ascii"), "result": result})
        u.reg_write(UC_X86_REG_EAX, result & 0xFFFFFFFF)
        u.reg_write(UC_X86_REG_ESP, stack + 4)  # cdecl import; caller removes argument.
        u.reg_write(UC_X86_REG_EIP, destination)

    def get(self, kind, text, out_of_bounds=False):
        raw = text.encode("ascii")
        assert len(raw) < 100 and b"\0" not in raw
        self.u.mem_write(OBJECT, bytes(0x600))
        self.u.mem_write(OBJECT, struct.pack("<II", 0x38013C48, 0x3811A468))
        self.u.mem_write(OBJECT + 4 + 8, struct.pack("<II", 1, 1))
        self.u.mem_write(OBJECT + 4 + 0x18, struct.pack("<I", COLUMNS))
        self.u.mem_write(COLUMNS, struct.pack("<I", CELL))
        if len(raw) <= 15:
            self.u.mem_write(CELL, raw + b"\0")
            self.u.mem_write(CELL + 0x10, struct.pack("<II", len(raw), 15))
        else:
            self.u.mem_write(TEXT, raw + b"\0")
            self.u.mem_write(CELL, struct.pack("<I", TEXT))
            self.u.mem_write(CELL + 0x10, struct.pack("<II", len(raw), 127))
        guard = bytes.fromhex("abcdef1234567890")
        self.u.mem_write(OUTPUT, struct.pack("<I", SENTINEL) + guard)
        self.u.mem_write(0x39417470, bytes(4))
        self.u.mem_write(STACK, struct.pack("<IIII", STOP, int(out_of_bounds), 0, OUTPUT))
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.u.reg_write(UC_X86_REG_ECX, OBJECT)
        self.stopped = False
        self.import_calls = []
        entry, width = {"int32": (0x3732D270, 4), "int16": (0x3732D2D0, 2),
                        "int8": (0x3732D1F0, 1)}[kind]
        self.u.emu_start(entry, 0, count=10000)
        assert self.stopped and self.u.reg_read(UC_X86_REG_EIP) == STOP
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK + 16
        output = bytes(self.u.mem_read(OUTPUT, 4))
        assert bytes(self.u.mem_read(OUTPUT + 4, len(guard))) == guard
        assert output[width:] == struct.pack("<I", SENTINEL)[width:]
        value = int.from_bytes(output[:width], "little", signed=True)
        diagnostics = struct.unpack("<I", self.u.mem_read(0x39417470, 4))[0]
        assert diagnostics == int(out_of_bounds)
        return {"kind": kind, "text": text, "outOfBounds": out_of_bounds,
                "returnBool": self.u.reg_read(UC_X86_REG_EAX) & 0xFF,
                "value": value, "outputHex": output.hex(), "imports": self.import_calls,
                "boundsDiagnostics": diagnostics}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hw", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    original = Original(args.hw, args.metadata)
    # Literal expectations are independently stated; there is no production
    # parser import or unsafe overflowing atoi input in this matrix.
    fixtures = [("5.5", 5), ("10.6", 10), ("0.1", 0), ("-5.5", -5),
        ("-0.1", 0), (" ", 0), ("\t\r\n", 0), ("", None),
        ("32767", 32767), ("32767.9", 32767), ("32768", 32768),
        ("65535", 65535), ("65536", 65536), ("-32768", -32768),
        ("-32769", -32769), ("2147483647", 2147483647),
        ("-2147483648", -2147483648), ("+7.9", 7), ("  -42junk", -42),
        ("123xyz", 123), ("1e3", 1), (".5", 0), ("xyz", 0),
        ("#comment", 0), ("0x10", 0), ("00000000000000000005.5", 5)]
    records, failures = [], []
    for kind, width in (("int32", 4), ("int16", 2), ("int8", 1)):
        for text, want in fixtures:
            row = original.get(kind, text)
            if want is None:
                expected = SENTINEL if width == 4 else 0
            else:
                bits = width * 8
                expected = ((want + (1 << (bits - 1))) % (1 << bits)) - (1 << (bits - 1))
            row["expected"] = expected
            row["expectedReturnBool"] = int(bool(text))
            records.append(row)
            if row["value"] != expected or row["returnBool"] != int(bool(text)):
                failures.append(row)
        row = original.get(kind, "unused", out_of_bounds=True)
        row["expected"] = SENTINEL if width == 4 else 0
        row["expectedReturnBool"] = 0
        records.append(row)
        if row["value"] != row["expected"] or row["returnBool"]:
            failures.append(row)
    result = {"level": "unaltered copied HW scalar/get-cell x86 with explicit external CRT substitution",
        "nativeDLLLoaded": False, "liveGameTest": False, "productionParserCompared": False,
        "hwSHA256": HW_SHA256, "cases": len(records), "failures": failures,
        "records": records, "substitutions": [{"iat": "0x3800fd68", "import": "atoi",
            "provider": "host ucrtbase.dll via ctypes", "scope": "non-overflowing ASCII strings only"}],
        "fixtureSetup": "rectangular 1x1 std::string table, inline and heap-backed cells",
        "overflowInputsExecuted": False,
        "inputs": [evidence(args.hw), evidence(args.metadata), evidence(Path(__file__))],
        "originalWindows": [{"start": hex(address), "bytes": count,
            "sha256": hashlib.sha256(original.original(address, count)).hexdigest()}
            for address, count in ((0x3732D270, 15), (0x3732D2D0, 46),
                                   (0x3732D1F0, 46), (0x37C15910, 87), (0x37C15B70, 69))]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"cases": len(records), "failures": len(failures)}))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
