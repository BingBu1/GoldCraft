#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original CSO crouch scalar windows and its friction-mode predicate."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import random
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ECX,
                              UC_X86_REG_ESP, UC_X86_REG_MXCSR,
                              UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM3)

ROOT = Path(__file__).resolve().parents[2]
CLIENT_SHA = "f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd"
SERVER_SHA = "39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2"


def identity(path):
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("client", "server", "client-evidence", "server-evidence", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    private = [args.client.resolve(strict=True), args.server.resolve(strict=True),
               args.client_evidence.resolve(strict=True), args.server_evidence.resolve(strict=True)]
    if out.exists() or not out.is_relative_to(ROOT / "build") or any(
            not path.is_relative_to(ROOT / "analysis") for path in private):
        raise ValueError("Use private workspace samples/evidence and fresh build output")
    reader_path = ROOT / "tools/Inspect-MetaHookRuntime.py"
    spec = importlib.util.spec_from_file_location("pe_reader", reader_path)
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    client, server = (reader.PE(path) for path in private[:2])
    if (client.sha256, server.sha256) != (CLIENT_SHA, SERVER_SHA):
        raise ValueError("Original module identities differ from the IDA evidence")

    def raw(image, base, start, size):
        offset = image.offset(start - base)
        return image.data[offset:offset + size]

    windows = []

    def verified(image, base, start, end, evidence):
        code = raw(image, base, start, end - start)
        instructions = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(code, start))
        queries = json.loads(evidence.read_text("utf-8"))["result"]
        query = next(row for row in queries if int(row["query"]["start"], 0) <= start
                     and int(row["query"]["end"], 0) >= end)
        rows = [row for row in query["matches"] if start <= int(row["addr"], 0) < end]
        if (query["truncated"] or not query["cursor"]["done"] or not instructions
                or instructions[-1].address + instructions[-1].size != end
                or [row.address for row in instructions] != [int(row["addr"], 0) for row in rows]):
            raise ValueError("Original instruction window differs from complete IDA evidence")
        windows.append(dict(start=hex(start), endExclusive=hex(end), instructions=len(instructions),
                            bytes=len(code), sha256=hashlib.sha256(code).hexdigest()))
        return code

    clamp_start, clamp_end = 0x114E70B7, 0x114E70C8
    scale_start, scale_end = 0x02AAB6CF, 0x02AAB72E
    gate_start, gate_end = 0x10A8FE30, 0x10A8FE4B
    clamp = verified(server, 0x10000000, clamp_start, clamp_end, private[3])
    scale = verified(client, 0x01900000, scale_start, scale_end, private[2])
    gate = verified(server, 0x10000000, gate_start, gate_end, private[3])
    maximum = struct.unpack("<f", raw(server, 0x10000000, 0x117BBA14, 4))[0]
    coefficient = struct.unpack("<d", raw(client, 0x01900000, 0x031A3748, 8))[0]
    if bits(maximum) != bits(.9) or coefficient != .333:
        raise ValueError("Original scalar constants changed")
    before = [identity(path) for path in [Path(__file__).resolve(), reader_path, *private]]
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    pages = set()

    def put(address, data):
        for page in range(address & ~4095, (address + len(data) + 4095) & ~4095, 4096):
            if page not in pages:
                machine.mem_map(page, 4096)
                pages.add(page)
        machine.mem_write(address, data)

    for address, code in ((clamp_start, clamp), (scale_start, scale), (gate_start, gate)):
        put(address, code)
    arena, stack, getter, method = 0x500000, 0x600000, 0x700000, 0x700100
    put(arena, bytes(0x60000))
    put(stack, bytes(0x3000))
    put(0x03EDE990, struct.pack("<I", arena))
    put(0x031A3748, struct.pack("<d", coefficient))
    machine.reg_write(UC_X86_REG_MXCSR, 0x1F80)
    generator = random.Random(33)
    values = [-math.inf, -2., -0., 0., .1, .5, .9, 1., math.inf, math.nan]
    values += [f32(generator.uniform(-1, 2)) for _ in range(256)]
    commands = [(0., 0., 0.), (400., -300., 125.), (-176.8, 221., 58.8744), (.0001, -.0001, 1.)]
    checks = 0
    for value in values:
        value = f32(value)
        machine.reg_write(UC_X86_REG_EAX, arena)
        machine.reg_write(UC_X86_REG_EBP, stack + 0x1000)
        machine.reg_write(UC_X86_REG_XMM0, 0)
        machine.reg_write(UC_X86_REG_XMM1, bits(maximum))
        machine.mem_write(stack + 0x1000 - 0x3C, struct.pack("<f", value))
        machine.emu_start(clamp_start, clamp_end, count=100)
        clamped = struct.unpack("<f", machine.mem_read(arena + 0x284, 4))[0]
        if math.isnan(value):
            assert math.isnan(clamped), "Original producer propagates an unordered attribute sum"
        else:
            assert bits(clamped) == bits(min(max(value, 0.), maximum)), "Original producer clamp"
        checks += 1
        for command in commands:
            machine.mem_write(arena + 0x220, struct.pack("<f", clamped))
            command = tuple(map(f32, command))
            machine.mem_write(arena + 0x4547C, struct.pack("<fff", *command))
            machine.reg_write(UC_X86_REG_ECX, arena)
            machine.reg_write(UC_X86_REG_XMM1, 0)
            machine.reg_write(UC_X86_REG_XMM3, bits(1.))
            machine.emu_start(scale_start, scale_end, count=100)
            actual = struct.unpack("<III", machine.mem_read(arena + 0x4547C, 12))
            boost = 0. if math.isnan(clamped) else max(clamped, 0.)
            factor = f32(f32(boost + 1.) * coefficient)
            assert actual == tuple(bits(f32(factor * component)) for component in command), "Original three-axis scalar precision"
            checks += 3

    # Execute the actual mode predicate. Only the engine's current-controller
    # getter and that controller's mode method are explicit ABI providers.
    controller, table, sentinel = arena + 0x800, arena + 0x1000, stack + 0x2000
    put(getter, b"\xc3")
    put(method, b"\xc3")
    put(0x12278834, struct.pack("<I", getter))
    machine.mem_write(controller, struct.pack("<I", table))
    machine.mem_write(table + 0x190, struct.pack("<I", method))
    current_mode = 0

    def provider(uc, address, size, user):
        if address == getter:
            uc.reg_write(UC_X86_REG_EAX, controller)
        elif address == method:
            assert uc.reg_read(UC_X86_REG_ECX) == controller, "Original thiscall receiver"
            uc.reg_write(UC_X86_REG_EAX, current_mode)

    machine.hook_add(UC_HOOK_CODE, provider, begin=getter, end=method)
    for current_mode in range(81):
        machine.mem_write(stack + 0x1000, struct.pack("<I", sentinel))
        machine.reg_write(UC_X86_REG_ESP, stack + 0x1000)
        machine.emu_start(gate_start, sentinel, count=100)
        assert machine.reg_read(UC_X86_REG_EAX) == (current_mode == 53), "Original friction mode gate"
        checks += 1
    stable = before == [identity(Path(row["path"])) for row in before]
    assert stable, "Original samples and test evidence changed"
    out.mkdir()
    receipt = dict(success=True, inputsUnchanged=stable, checks=checks, windows=windows,
                   inputs=before, scalarInputs=len(values), commandVectors=len(commands), modeInputs=81,
                   originalAttributeMaximum=maximum, originalCrouchCoefficient=coefficient,
                   mode33UsesNormalFriction=True, productionChanged=False, deployed=False,
                   boundary="Original scalar and mode-predicate instructions only; active-effect storage, full PM/network and gameplay are separate")
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", "utf-8")
    print(json.dumps({key: value for key, value in receipt.items() if key not in ("inputs", "windows")}))


if __name__ == "__main__":
    main()
