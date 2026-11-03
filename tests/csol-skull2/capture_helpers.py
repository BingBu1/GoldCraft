#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture SKULL2 spread and trigger/accuracy prefixes from the matched MP image."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP


def identity(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mp', type=Path, required=True)
    ap.add_argument('--properties', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir()
    inputs = [identity(args.mp), identity(args.properties), identity(__file__)]
    assert inputs[0]['sha256'] == '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'
    props = json.loads(args.properties.read_text('utf-8-sig'))
    assert props['moduleSha256'] == inputs[0]['sha256']
    properties = {int(row['offset'], 16): int(row['bits'], 16) for row in props['properties']}
    data = args.mp.read_bytes()
    u16 = lambda at: struct.unpack_from('<H', data, at)[0]
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    nt = u32(0x3c)
    assert data[nt:nt + 4] == b'PE\0\0' and u16(nt + 4) == 0x14c
    optional = nt + 24
    assert u16(optional) == 0x10b
    base = u32(optional + 28)
    table = optional + u16(nt + 20)
    sections = [(u32(table + i * 40 + 12), u32(table + i * 40 + 16),
                 u32(table + i * 40 + 20), u32(table + i * 40 + 8)) for i in range(u16(nt + 6))]
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    size = max(rva + max(raw_size, virtual) for rva, raw_size, raw, virtual in sections)
    machine.mem_map(base, (size + 4095) & ~4095)
    for rva, raw_size, raw, virtual in sections:
        machine.mem_write(base + rva, data[raw:raw + raw_size])
    scratch = 0x30000000
    machine.mem_map(scratch, 0x100000)
    obj, owner, pev, globals_at, props_at, stack, stop, float_stub, float_value, query_stub = [
        scratch + n for n in (0, 0x10000, 0x20000, 0x30000, 0x40000, 0x80000,
                             0xf0000, 0xf1000, 0xf2000, 0xf3000)]
    read32 = lambda at: struct.unpack('<I', machine.mem_read(at, 4))[0]
    write32 = lambda at, value: machine.mem_write(at, struct.pack('<I', value & 0xffffffff))
    writef = lambda at, value: machine.mem_write(at, struct.pack('<f', value))
    machine.mem_write(float_stub, b'\xd9\x05' + struct.pack('<I', float_value) + b'\xc2\x04\x00')
    machine.mem_write(query_stub, b'\x31\xc0\xc3')
    write32(0x122788f4, globals_at)
    write32(0x12278834, query_stub)
    ranges = [(0x10f831a0, 0x10f836e0), (0x10f83980, 0x10f83a9c),
              (0x10f83fdd, 0x10f83ff0), (0x10f83ff0, 0x10f84110)]
    observed, outcome = set(), {}
    operation = ''

    def hook(uc, address, length, unused):
        if operation == 'S' and address in (0x10f83980, 0x10f83ff0):
            sp = machine.reg_read(UC_X86_REG_ESP)
            outcome.update(spreadBits=read32(sp + 4), intervalBits=read32(sp + 8))
            machine.emu_stop()
        elif address in (stop, 0x10f83a9c, 0x10f84110):
            outcome['allowed'] = address != stop
            machine.emu_stop()
        elif float_stub <= address < float_stub + 9 or query_stub <= address < query_stub + 3:
            return
        elif any(start <= address < end for start, end in ranges):
            observed.add(address)
        elif address == 0x105feb30:
            offset = machine.reg_read(UC_X86_REG_ECX) - props_at
            assert offset in properties, 'unexpected property: ' + hex(offset)
            write32(float_value, properties[offset])
            machine.reg_write(UC_X86_REG_EIP, float_stub)
        else:
            raise AssertionError('unexpected original target ' + hex(address))

    machine.hook_add(UC_HOOK_CODE, hook)
    initial = machine.context_save()

    def run(values):
        nonlocal operation
        operation = values['operation']
        machine.context_restore(initial)
        machine.mem_write(obj, bytes(0x30000))
        write32(obj + 0xd0, owner)
        write32(owner + 8, pev)
        write32(obj + 0x204, props_at)
        write32(obj + 0x1c0, values['flags'])
        writef(globals_at, values['now'])
        write32(obj + 0x198, properties[0x930 if values['flags'] & 8 else 0x230])
        outcome.clear()
        if operation == 'S':
            writef(owner + 0x2eb0, values['now'] + (1 if values['air'] else -1))
            machine.mem_write(owner + 0x192d, bytes([values['standing']]))
            write32(pev + 0x1c8, (0x200 if values['grounded'] else 0) | (0x4000 if values['ducking'] else 0))
            writef(pev + 0x20, values['speed'])
            address = 0x10f831a0
        else:
            write32(obj + 0x1a0, values['shots'])
            writef(obj + 0x19c, values['previous'])
            address = 0x10f83980 if values['flags'] & 8 else 0x10f83ff0
        machine.reg_write(UC_X86_REG_ECX, obj)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.mem_write(stack, struct.pack('<IIII', stop, 0, 0, 0))
        machine.emu_start(address, stop + 1, timeout=1_000_000, count=5000)
        assert outcome
        return dict(outcome, accuracyBits=read32(obj + 0x198), shots=read32(obj + 0x1a0),
                    lastFireBits=read32(obj + 0x19c))

    cases = []
    result = dict(success=False, inputs=inputs, moduleSha256=inputs[0]['sha256'], cases=cases,
        boundary='Matched original Primary to Fire arguments and Fire trigger/accuracy prefixes; property/API spies only. No engine/game.')
    try:
        for flags, grounded, ducking, speed, air, standing in itertools.product(
                (4, 8, 16, 32, 12), (False, True), (False, True), (0, .001, 1, 250), (False, True), (False, True)):
            values = dict(operation='S', flags=flags, grounded=grounded, ducking=ducking, speed=speed,
                          air=air, standing=standing, now=10)
            cases.append(dict(input=values, output=run(values)))
        for flags, shots, previous, now in itertools.product((4, 8, 16, 32, 12), (0, 1, 9),
                (-1, 0, .001, 9.7, 9.9, 10, 11, 100000), (-1, 0, 10, 100000)):
            values = dict(operation='A', flags=flags, shots=shots, previous=previous, now=now)
            cases.append(dict(input=values, output=run(values)))
        result['success'] = True
    except BaseException as error:
        result['error'] = str(error)
    finally:
        result['caseCount'] = len(cases)
        result['executedInstructionAddresses'] = list(map(hex, sorted(observed)))
        result['ranges'] = [dict(start=hex(start), end=hex(end), sha256=hashlib.sha256(
            bytes(machine.mem_read(start, end - start))).hexdigest()) for start, end in ranges]
        result['changedInputs'] = [row['path'] for row in inputs if identity(row['path']) != row]
        if result['changedInputs']: result['success'] = False
        (args.output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps({key: value for key, value in result.items() if key not in ('cases', 'executedInstructionAddresses')}))
    return 0 if result['success'] else 1


if __name__ == '__main__': raise SystemExit(main())
