# SPDX-License-Identifier: GPL-3.0-or-later
"""Run bounded original MP selection/narrowing/setter instructions offline.

The MP file is read-only and pinned. The only substituted call is the CRT rand
import, whose supplied return value is part of each observation. This is not a
whole lifecycle/gameplay emulator. Original bytes must also match saved IDA
get_bytes evidence before they are executed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

MP_SHA = '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'


def identity(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def generate(mp, evidence, output):
    before = identity(mp)
    assert before['sha256'] == MP_SHA
    raw_module = mp.read_bytes()
    nt = struct.unpack_from('<I', raw_module, 0x3c)[0]
    machine, section_count = struct.unpack_from('<HH', raw_module, nt + 4)
    optional_size = struct.unpack_from('<H', raw_module, nt + 20)[0]
    optional = nt + 24
    base = struct.unpack_from('<I', raw_module, optional + 28)[0]
    image_size = struct.unpack_from('<I', raw_module, optional + 56)[0]
    assert base == 0x10000000 and machine == 0x14c
    sections = []
    for i in range(section_count):
        section = optional + optional_size + i * 40
        virtual_size, rva, raw_size, file_offset = struct.unpack_from('<4I', raw_module, section + 8)
        sections.append((rva, raw_module[file_offset:file_offset + raw_size]))

    def original(start, size):
        for rva, data in sections:
            if rva <= start - base < rva + len(data):
                offset = start - base - rva
                result = data[offset:offset + size]
                assert len(result) == size
                return result
        raise ValueError(hex(start))
    verified = []
    for name, start, end in (
        ('route-selection', 0x1137fdbc, 0x1137ff45),
        ('goal-room-next-after', 0x113845b0, 0x11384629),
        ('room-controller-setter-after', 0x113a33f0, 0x113a343e),
        ('final-room', 0x1137c350, 0x1137c3bc),
    ):
        path = evidence / (name + '-ida-bytes.json')
        doc = json.loads(path.read_text('utf8'))
        if 'structuredContent' in doc:
            doc = doc['structuredContent']
        elif 'content' in doc:
            doc = json.loads(doc['content'][0]['text'])
        raw = bytes(int(t, 16) for t in doc['result'][0]['data'].replace(',', ' ').split())
        assert raw == original(start, end - start)
        verified.append(dict(start=hex(start), end=hex(end), bytes=len(raw),
                             sha256=hashlib.sha256(raw).hexdigest(), ida=identity(path)))
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(base, (image_size + 4095) & ~4095)
    for rva, data in sections:
        mu.mem_write(base + rva, data)
    manager, controller, stack = 0x500000, 0x510000, 0x600000
    mu.mem_map(manager, 0x30000)
    mu.mem_map(stack, 0x10000)
    random_value = 0

    def hook(uc, address, size, _):
        if address == 0x1137fde8:
            uc.reg_write(UC_X86_REG_EAX, random_value)
            uc.reg_write(UC_X86_REG_EIP, 0x1137fdee)

    mu.hook_add(UC_HOOK_CODE, hook)

    def u32(address, value):
        mu.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def reset():
        mu.reg_write(UC_X86_REG_ESP, stack + 0x8000)
        mu.reg_write(UC_X86_REG_EBP, stack + 0x8100)
        mu.reg_write(UC_X86_REG_EFLAGS, 2)

    selection = []
    overrides = [-5.0, -0.0, 0.125, 0.5, 0.999, 1.0, 1.9, 2.0, 3.0, 4.0,
                 17.25, 65535.75, 2147483520.0]
    for count in (1, 2, 3, 7, 17):
        for override in overrides:
            for random_value in (0, 1, 2, 32767, 0xffffffff):
                reset()
                mu.reg_write(UC_X86_REG_ESI, manager)
                u32(manager + 0x140, 0x520000)
                u32(manager + 0x144, 0x520000 + count * 116)
                mu.mem_write(0x11dba0fc, struct.pack('<f', override))
                mu.emu_start(0x1137fdd1, 0x1137fe23, count=100)
                actual = struct.unpack('<I', mu.mem_read(manager + 0x135c, 4))[0]
                selection.append(dict(count=count, override=override, random=random_value, actual=actual))
    rooms = []
    for count in (1, 7, 300, 32768):
        for destination in (-2147483648, -65536, -1, 0, 1, 2, 7, 8, 32767,
                            32768, 32769, 65535, 65536, 65537, 2147483647):
            for narrowing in (False, True):
                reset()
                if narrowing:
                    mu.reg_write(UC_X86_REG_EAX, manager)
                    u32(manager + 0x20, destination)
                    mu.emu_start(0x1138460d, 0x11384619, count=20)
                    requested = mu.reg_read(UC_X86_REG_EAX)
                else:
                    requested = (destination - 1) & 0xffffffff
                reset()
                u32(0x12228e28, manager)
                u32(manager + 0xd4, 0x520000)
                u32(manager + 0xd8, 0x520000 + count * 44)
                u32(stack + 0x8000, 0x700000)
                u32(stack + 0x8004, 1)
                u32(stack + 0x8008, requested)
                mu.reg_write(UC_X86_REG_ECX, controller)
                mu.emu_start(0x113a33f0, 0x113a343b, count=100)
                actual = struct.unpack('<h', mu.mem_read(controller + 0x2bc + 0x202, 2))[0]
                rooms.append(dict(count=count, destination=destination, narrowing=narrowing, actual=actual))
    assert identity(mp) == before
    doc = dict(module=before, verified=verified, selections=selection, rooms=rooms,
               scope='Bounded original instructions; CRT rand return controlled; no engine Replay claim')
    output.write_text(json.dumps(doc, indent=2), 'utf8')
    return doc


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mp', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = generate(args.mp, args.evidence, args.output)
    print(json.dumps(dict(selections=len(result['selections']), rooms=len(result['rooms']))))
