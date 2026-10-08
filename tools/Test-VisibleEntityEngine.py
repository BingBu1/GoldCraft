"""Execute the exact copied x86 engine's visible-entity insertion in Unicorn.

Requires the local unicorn Python package. The game binary is never
published or modified. Independent of the live client and input devices.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, default=ROOT / "analysis/engine/input-10210/hw.dll")
    args = parser.parse_args()
    engine = args.engine.resolve(strict=True)
    if not engine.is_relative_to(ROOT):
        raise ValueError("Use an engine copy inside the workspace")
    spec = importlib.util.spec_from_file_location("visible_catalog", ROOT / "tools/Build-VisibleEntityGameData.py")
    catalog = importlib.util.module_from_spec(spec); spec.loader.exec_module(catalog)
    config = catalog.validate(engine)
    data = engine.read_bytes()
    pe = struct.unpack_from("<I", data, 60)[0]
    count = struct.unpack_from("<H", data, pe+6)[0]
    optional = struct.unpack_from("<H", data, pe+20)[0]
    base = struct.unpack_from("<I", data, pe+24+28)[0]
    size = struct.unpack_from("<I", data, pe+24+56)[0]
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(base, (size+4095) & ~4095)
    for i in range(count):
        _, rva, length, offset = struct.unpack_from("<IIII", data, pe+24+optional+i*40+8)
        if length: mu.mem_write(base+rva, data[offset:offset+length])
    stack, replacement, marker, callback = 0x20000000, 0x21001000, 0x22000000, 0x23000000
    mu.mem_map(stack, 0x10000); mu.mem_map(replacement-4096, 0x6000); mu.mem_map(callback, 4096)
    mu.mem_write(callback, b"\xc3")
    # The public API recorder callback is an inert NullDst in this engine.
    # Replace only that external callback with a return in the emulator.
    mu.mem_write(base+0x31ba9c, struct.pack("<I", callback))
    native_list, native_count = base+0x1252f40, base+0x1253740
    mu.mem_write(replacement-4, b"GUAR"); mu.mem_write(replacement+4096*4, b"DEND")

    def insert(index):
        sp = stack+0x8000
        mu.mem_write(sp, struct.pack("<III", callback+16, 0, marker+index*16))
        mu.reg_write(UC_X86_REG_ESP, sp)
        mu.emu_start(base+0x196180, callback+16, count=100)
        return mu.reg_read(UC_X86_REG_EAX)

    def integer(address):
        return struct.unpack("<I", mu.mem_read(address, 4))[0]

    checks = {}
    mu.mem_write(native_count, struct.pack("<I", 511))
    checks["stockAccepts512"] = insert(511) == 1 and integer(native_count) == 512
    checks["stockRejects513"] = insert(512) == 0 and integer(native_count) == 512
    stock_data = bytes(mu.mem_read(native_list, 512*4))
    for patch in config["patches"]:
        value = replacement if patch["kind"] == "array" else 4096
        mu.mem_write(base+int(patch["operandRva"], 16), struct.pack("<I", value))
    # Unicorn caches translated blocks across emu_start calls. Explicitly
    # invalidate the stock insertion block after modifying its instructions.
    mu.ctl_remove_cache(base, base+size-1)
    mu.mem_write(native_count, struct.pack("<I", 0))
    results = [insert(i) for i in range(4096)]
    checks["accepts4096DistinctPointers"] = all(x == 1 for x in results) and integer(native_count) == 4096
    checks["allSlotsPreserveIdentity"] = list(struct.unpack("<4096I", mu.mem_read(replacement, 4096*4))) == [marker+i*16 for i in range(4096)]
    checks["rejects4097"] = insert(4096) == 0 and integer(native_count) == 4096
    checks["allocationCanariesUnchanged"] = bytes(mu.mem_read(replacement-4, 4)) == b"GUAR" and bytes(mu.mem_read(replacement+4096*4, 4)) == b"DEND"
    checks["legacyArrayUnchanged"] = bytes(mu.mem_read(native_list, 512*4)) == stock_data
    mu.mem_write(native_count, struct.pack("<I", 0))
    checks["nextFrameStartsAtZero"] = insert(9000) == 1 and integer(native_count) == 1 and integer(replacement) == marker+9000*16
    report = {"engineSha256": config["sha256"], "scope": "Actual x86 insertion; not a visual or network-edict test", "checks": checks, "passed": all(checks.values())}
    destination = ROOT / "analysis/visible-entities/x86-boundary.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))
    if not report["passed"]: raise AssertionError("x86 visible-entity boundary failure")


if __name__ == "__main__":
    main()
