"""Replay verified host CRC instructions without loading or initializing its DLL.

The game binary is a local input only. No original code bytes are published.
This checks nonnegative inputs; negative lengths are not a supported host input.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import zlib

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("pe_audit", ROOT / "tools/Inspect-MetaHookRuntime.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
EXPECTED = "9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / "sandbox/cs-client-b/Half-Life/hw.dll")
    parser.add_argument("--output", type=Path, default=ROOT / "build/client-crc32/replay.json")
    args = parser.parse_args()
    pe = AUDIT.PE(args.host)
    if pe.sha256 != EXPECTED:
        raise ValueError("Unsupported host identity")
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output.exists():
        raise ValueError("Choose an unused output under build")
    base, stack, state, buffer, stop = 0x10000000, 0x100000, 0x200000, 0x300000, 0x400000
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for address, size in ((base + 0x1BC000, 0x1000), (base + 0x2BA000, 0x1000),
                          (stack, 0x2000), (state, 0x1000), (buffer, 0x20000), (stop, 0x1000)):
        uc.mem_map(address, size)
    ranges = []
    for rva, size in ((0x1BC430, 0x20D), (0x2BA290, 1024)):
        data = pe.data[pe.offset(rva):pe.offset(rva) + size]
        uc.mem_write(base + rva, data)
        ranges.append({"rva": hex(rva), "bytes": size, "sha256": hashlib.sha256(data).hexdigest()})
    active = None
    reads = 0

    def memory_read(_uc, _kind, address, size, _value, _data):
        nonlocal reads
        if active is not None and buffer <= address < buffer + 0x20000:
            reads += 1
            if not active[0] <= address or address + size > active[1]:
                raise AssertionError("Original CRC read beyond input interval")

    uc.hook_add(UC_HOOK_MEM_READ, memory_read)

    def call(rva, args):
        sp = stack + 0x1000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(args) + 1), stop, *args))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.emu_start(base + rva, stop, count=3000000)
        if uc.reg_read(UC_X86_REG_ESP) != sp + 4:
            raise AssertionError("Host function did not return through cdecl frame")

    def process(data, seed, alignment):
        nonlocal active
        address = buffer + alignment
        uc.mem_write(address, data or b"\xa5")
        uc.mem_write(state, struct.pack("<I", seed))
        active = address, address + len(data)
        call(0x1BC450, [state, address, len(data)])
        active = None
        return struct.unpack("<I", uc.mem_read(state, 4))[0]

    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
        table.append(crc)
    assert bytes(uc.mem_read(base + 0x2BA290, 1024)) == struct.pack("<256I", *table)
    call(0x1BC440, [state])
    assert bytes(uc.mem_read(state, 4)) == b"\xff" * 4
    call(0x1BC430, [0x12345678])
    assert uc.reg_read(UC_X86_REG_EAX) == 0xEDCBA987
    rng, cases, splits = random.Random(0x20261107), 0, 0
    for length in [*range(130), 255, 256, 257, 1023, 1024, 1025, 4096, 8192, 65535]:
        data = rng.randbytes(length)
        for alignment in range(8):
            seed = rng.getrandbits(32)
            assert process(data, seed, alignment) == (zlib.crc32(data, seed ^ 0xFFFFFFFF) ^ 0xFFFFFFFF)
            cases += 1
    for length in (0, 1, 7, 8, 9, 15, 16, 17, 63, 64, 65, 1024):
        data = rng.randbytes(length)
        for split in range(min(length + 1, 20)):
            seed = rng.getrandbits(32)
            assert process(data, seed, 3) == process(data[split:], process(data[:split], seed, 5), 1)
            splits += 1
    assert process(b"123456789", 0xFFFFFFFF, 0) ^ 0xFFFFFFFF == 0xCBF43926
    result = {"hostSha256": pe.sha256, "ranges": ranges, "cases": cases, "splitCalls": splits,
              "checkedInputReads": reads, "tableEntries": 256, "passed": True,
              "negativeLength": "not supported/tested; optimization must preserve original fallback",
              "liveGameplay": False}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
