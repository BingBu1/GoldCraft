#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute matching original SKULL2 instructions; never overwrite earlier evidence."""
import argparse
import hashlib
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
    section_table = optional + u16(nt + 20)
    sections = []
    for i in range(u16(nt + 6)):
        at = section_table + i * 40
        sections.append(dict(rva=u32(at + 12), size=u32(at + 16), raw=u32(at + 20), virtual=u32(at + 8)))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    size = max(row['rva'] + max(row['size'], row['virtual']) for row in sections)
    machine.mem_map(base, (size + 4095) & ~4095)
    for row in sections:
        machine.mem_write(base + row['rva'], data[row['raw']:row['raw'] + row['size']])
    scratch = 0x30000000
    machine.mem_map(scratch, 0x100000)
    obj, owner, pev, globals_at, props_at, stack, stop, float_stub, float_value = [scratch + n for n in
        (0, 0x10000, 0x20000, 0x30000, 0x40000, 0x80000, 0xf0000, 0xf1000, 0xf2000)]
    read32 = lambda at: struct.unpack('<I', machine.mem_read(at, 4))[0]
    write32 = lambda at, value: machine.mem_write(at, struct.pack('<I', value))
    writef = lambda at, value: machine.mem_write(at, struct.pack('<f', value))
    # Explicit property API spy: fld captured float32; ret 4. This does not
    # implement any state branch or replace an original weapon instruction.
    machine.mem_write(float_stub, b'\xd9\x05' + struct.pack('<I', float_value) + b'\xc2\x04\x00')
    ranges = [(0x10f837e0, 0x10f83975), (0x10f82f80, 0x10f83130),
              (0x10f82b80, 0x10f82b96), (0x10f82ee0, 0x10f82f15),
              (0x10f82a20, 0x10f82a93), (0x10f82ae0, 0x10f82b7a),
              (0x10612430, 0x10612433), (0x1061ce20, 0x1061ce26)]
    animations, spies = [], []
    observed = set()

    def finish(pop=0, eax=None):
        sp = machine.reg_read(UC_X86_REG_ESP)
        destination = read32(sp)
        machine.reg_write(UC_X86_REG_ESP, sp + 4 + pop)
        if eax is not None:
            machine.reg_write(UC_X86_REG_EAX, eax)
        machine.reg_write(UC_X86_REG_EIP, destination)

    def hook(uc, address, length, unused):
        if address == stop:
            machine.emu_stop()
        elif float_stub <= address < float_stub + 9:
            return
        elif any(start <= address < end for start, end in ranges):
            observed.add(address)
        elif address == 0x11540360:
            spies.append('catalog max clip=7')
            finish(eax=7)
        elif address == 0x11542950:
            spies.append('base ItemPostFrame omitted')
            finish()
        elif address == 0x11541b30:
            spies.append('base Holster omitted')
            finish(pop=4)
        elif address == 0x11547af0:
            sp = machine.reg_read(UC_X86_REG_ESP)
            animations.append(dict(sequence=read32(sp + 4), skiplocal=read32(sp + 8)))
            finish(pop=8)
        elif address == 0x105feb30:
            offset = machine.reg_read(UC_X86_REG_ECX) - props_at
            assert offset in (0x230, 0x930), 'unexpected property getter'
            spies.append('property getter captured offset=' + hex(offset))
            write32(float_value, properties[offset])
            machine.reg_write(UC_X86_REG_EIP, float_stub)
        else:
            raise AssertionError('unexpected original target ' + hex(address))

    machine.hook_add(UC_HOOK_CODE, hook)
    write32(0x122788f4, globals_at)

    def run(flags, clip, reserve, operation, now, deadline=13):
        machine.mem_write(obj, bytes(0x30000))
        write32(obj, 0x118f6554)
        write32(obj + 0xd0, owner)
        write32(owner + 8, pev)
        write32(obj + 0x204, props_at)
        write32(obj + 0x160, clip)
        write32(obj + 0x1c0, flags)
        write32(owner + 0x1054, reserve)
        writef(globals_at, now)
        writef(obj + 0x220, deadline)
        animations.clear()
        spies.clear()
        address = {'B': 0x10f837e0, 'F': 0x10f82f80, 'H': 0x10f82ee0,
                   'N': 0x10f82a20, 'D': 0x10f82ae0}[operation]
        machine.reg_write(UC_X86_REG_ECX, obj)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.mem_write(stack, struct.pack('<II', stop, 0))
        machine.emu_start(address, stop, timeout=1_000_000, count=5000)
        assert machine.reg_read(UC_X86_REG_EIP) == stop
        return dict(flags=read32(obj + 0x1c0), clip=read32(obj + 0x160), reserve=read32(owner + 0x1054),
                    deadlineBits=read32(obj + 0x220), accuracyBits=read32(obj + 0x198),
                    pmodel=read32(pev + 0xd0), animations=list(animations), spies=list(spies))

    cases = []
    result = dict(success=False, inputs=inputs, moduleSha256=inputs[0]['sha256'], cases=cases,
                  boundary='Original weapon instructions only, explicit base/catalog/property/animation API spies; no game or SDK.')
    try:
        for extra in (0, 0x101):
            for low in (0, 2, 4, 6, 8, 10, 16, 18, 32, 34, 12, 20, 24, 36, 40, 48, 60, 62):
                for clip in (0, 1, 7, 14, 15, 31):
                    for reserve in (0, 35):
                        for operation, now, deadline in [('B', 10, 5), ('F', 13, 13), ('F', 13.001, 13),
                                                         ('H', 10, 13), ('N', 10, 13), ('D', 10, 13)]:
                            values = dict(flags=extra | low, clip=clip, reserve=reserve,
                                          operation=operation, now=now, deadline=deadline)
                            cases.append(dict(input=values, output=run(**values)))
        result['success'] = True
    except BaseException as error:
        result['error'] = str(error)
    finally:
        result['caseCount'] = len(cases)
        result['executedInstructionAddresses'] = list(map(hex, sorted(observed)))
        result['ranges'] = [dict(start=hex(start), end=hex(end), sha256=hashlib.sha256(
            bytes(machine.mem_read(start, end - start))).hexdigest()) for start, end in ranges]
        result['changedInputs'] = [row['path'] for row in inputs if identity(row['path']) != row]
        if result['changedInputs']:
            result['success'] = False
        (args.output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(json.dumps({key: value for key, value in result.items() if key not in ('cases', 'executedInstructionAddresses')}))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
