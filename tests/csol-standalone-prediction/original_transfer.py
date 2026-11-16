#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original CSO prediction-field copies and mutable PlayerMove arguments."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX,
                              UC_X86_REG_ESI, UC_X86_REG_ESP)

ROOT = Path(__file__).resolve().parents[2]
CLIENT_SHA = "f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd"
ENGINE_SHA = "c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783"


def identity(path):
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def word(value):
    return struct.pack("<I", value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("client", "engine", "client-evidence", "engine-evidence", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    private = [args.client.resolve(strict=True), args.engine.resolve(strict=True),
               args.client_evidence.resolve(strict=True), args.engine_evidence.resolve(strict=True)]
    out = args.output.resolve()
    if (out.exists() or not out.is_relative_to(ROOT / "build")
            or any(not path.is_relative_to(ROOT / "analysis") for path in private)):
        raise ValueError("Use private workspace inputs and a fresh build output")
    reader_path = ROOT / "tools/Inspect-MetaHookRuntime.py"
    spec = importlib.util.spec_from_file_location("pe_reader", reader_path)
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    client, engine = (reader.PE(path) for path in private[:2])
    if (client.sha256, engine.sha256) != (CLIENT_SHA, ENGINE_SHA):
        raise ValueError("Original PE identities differ from the IDA evidence")
    inputs = [identity(path) for path in [Path(__file__).resolve(), reader_path, *private]]
    windows = []

    def verified(image, base, start, end, evidence):
        code = image.data[image.offset(start - base):image.offset(start - base) + end - start]
        decoded = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(code, start))
        queries = json.loads(evidence.read_text("utf-8"))["result"]
        query = next(row for row in queries if int(row["query"]["start"], 0) <= start
                     and int(row["query"]["end"], 0) >= end)
        rows = [row for row in query["matches"] if start <= int(row["addr"], 0) < end]
        if (query["error"] or query["truncated"] or not query["cursor"]["done"] or not decoded
                or decoded[-1].address + decoded[-1].size != end
                or [row.address for row in decoded] != [int(row["addr"], 0) for row in rows]):
            raise ValueError("Original code window lacks complete matching IDA instruction boundaries")
        windows.append(dict(start=hex(start), endExclusive=hex(end), instructions=len(decoded),
                            bytes=len(code), sha256=hashlib.sha256(code).hexdigest()))
        return code

    copy_in, copy_in_end = 0x3784EB53, 0x3784EB64
    copy_out, copy_out_end = 0x3784EF07, 0x3784EF18
    wrapper, wrapper_end = 0x02A2D460, 0x02A2D485
    code_windows = (
        (copy_in, verified(engine, 0x37100000, copy_in, copy_in_end, private[3])),
        (copy_out, verified(engine, 0x37100000, copy_out, copy_out_end, private[3])),
        (wrapper, verified(client, 0x01900000, wrapper, wrapper_end, private[2])),
    )
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    mapped = set()

    def put(address, data):
        for page in range(address & ~4095, (address + len(data) + 4095) & ~4095, 4096):
            if page not in mapped:
                machine.mem_map(page, 4096)
                mapped.add(page)
        machine.mem_write(address, data)

    def read_word(address):
        return struct.unpack("<I", machine.mem_read(address, 4))[0]

    def invoke(start, end):
        machine.emu_start(start, end, count=200)

    arena, stack = 0x500000, 0x600000
    source, destination, pm, alternate = arena, arena + 0x1000, arena + 0x2000, arena + 0x3000
    table, callback, downstream, stop = arena + 0x4000, 0x700000, 0x02AAD980, 0x700100
    put(arena, bytes(0x5000))
    put(stack, bytes(0x3000))
    for address, code in code_windows:
        put(address, code)
    put(0x39256234, word(pm))
    put(0x03AE8D2C, word(table))
    put(table + 0x18, word(callback))
    # Explicit callback/inner-PM ABI providers; neither transfer nor wrapper is replaced.
    put(callback, b"\xC3")
    put(downstream, b"\xC3")
    put(stop, b"\x90")
    checks = 0

    def check(condition):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(f"Original transfer check {checks} failed")

    generator = random.Random(33)
    values = [0, 0x80000000, 0x3F800000, 0x7F800000, 0xFF800000, 0x7FC00001,
              0x7F800001, 0xFFFFFFFF, *[generator.getrandbits(32) for _ in range(256)]]
    for value in values:
        put(source, bytes([0xA5]) * 0x800)
        put(pm, bytes([0x5A]) * 0x800)
        put(destination, bytes([0xC3]) * 0x800)
        put(source + 0x30C, word(value))
        machine.reg_write(UC_X86_REG_EBX, source)
        invoke(copy_in, copy_in_end)
        check(read_word(pm + 0x220) == value)
        check(bytes(machine.mem_read(pm, 0x800)) == b"\x5A" * 0x220
              + word(value) + b"\x5A" * (0x800 - 0x224))
        machine.reg_write(UC_X86_REG_ESI, destination)
        invoke(copy_out, copy_out_end)
        check(read_word(destination + 0x30C) == value)
        check(bytes(machine.mem_read(destination, 0x800)) == b"\xC3" * 0x30C
              + word(value) + b"\xC3" * (0x800 - 0x310))

    scenarios = ((pm, 0, pm, 0), (pm, 1, alternate, 0), (alternate, 0, pm, 1))
    events = []
    current = None

    def boundary(uc, address, size, _):
        esp = uc.reg_read(UC_X86_REG_ESP)
        if address == callback:
            first, second = read_word(esp + 4), read_word(esp + 8)
            events.append(("callback", read_word(first), read_word(second)))
            check((first, second) == (stack + 0x1004, stack + 0x1008))
            put(first, word(current[2]))
            put(second, word(current[3]))
        elif address == downstream:
            events.append(("pm", read_word(esp + 4), read_word(esp + 8)))

    machine.hook_add(UC_HOOK_CODE, boundary)
    for current in scenarios:
        events.clear()
        put(stack + 0x1000, word(stop) + word(current[0]) + word(current[1]))
        machine.reg_write(UC_X86_REG_ESP, stack + 0x1000)
        machine.reg_write(UC_X86_REG_EBP, 0x12345678)
        machine.reg_write(UC_X86_REG_EAX, 0xDEADBEEF)
        invoke(wrapper, stop)
        check(events == [("callback", current[0], current[1]), ("pm", current[2], current[3])])
        check(machine.reg_read(UC_X86_REG_ESP) == stack + 0x1004)
        check(machine.reg_read(UC_X86_REG_EBP) == 0x12345678)
    unchanged = all(identity(Path(row["path"])) == row for row in inputs)
    if not unchanged:
        raise ValueError("Original transfer inputs changed during replay")
    out.mkdir(parents=True)
    receipt = dict(success=True, inputsUnchanged=unchanged, checks=checks, values=len(values),
                   callbackScenarios=len(scenarios), windows=windows, inputs=inputs,
                   productionChanged=False, deployed=False, gameplay=False,
                   originalRuntimePluginResolved=False,
                   boundary="Original transfer instructions and mutable callback ABI only; "
                            "network producer, actual plugin, full PM and gameplay remain separate")
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in receipt.items()
                      if not isinstance(value, (list, dict))}))


if __name__ == "__main__":
    main()
