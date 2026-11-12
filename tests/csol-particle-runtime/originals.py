# SPDX-License-Identifier: GPL-3.0-or-later
"""Unchanged original ordinary-manager instructions; controlled external ABI.

Private x86 client and complete IDA/raw captures are mandatory. The DLL is
mapped into Unicorn, never loaded as a native library. This is not a renderer.
"""
import hashlib
import json
import struct

import capstone
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                              UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_XMM0)

CLIENT_SHA = 'f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
MANAGER, DRAW, VECTOR, NODES = 0x600000, 0x610000, 0x620000, 0x630000
PAYLOAD, ENTITY, STACK, STOP = 0x660000, 0xd00000, 0xeff000, 0x500000


def identity(path):
    for p in (path, *path.parents):
        if p.is_symlink() or getattr(p, 'is_junction', lambda: False)():
            raise ValueError('Linked oracle input: ' + str(p))
    path = path.resolve(strict=True)
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def raw_rows(value):
    if isinstance(value, dict):
        if isinstance(value.get('addr'), str) and isinstance(value.get('data'), str):
            yield int(value['addr'], 16), bytes(int(x, 16) for x in value['data'].split())
        for item in value.values():
            yield from raw_rows(item)
    elif isinstance(value, list):
        for item in value:
            yield from raw_rows(item)


class Original:
    def __init__(self, client, evidence):
        self.inputs = [identity(client)]
        assert self.inputs[0]['sha256'] == CLIENT_SHA, 'Original client identity'
        raw = client.read_bytes()
        nt = struct.unpack_from('<I', raw, 0x3c)[0]
        assert raw[nt:nt + 4] == b'PE\0\0' and struct.unpack_from('<H', raw, nt + 4)[0] == 0x14c
        opt = nt + 24
        base = struct.unpack_from('<I', raw, opt + 28)[0]
        size = struct.unpack_from('<I', raw, opt + 56)[0]
        assert base == 0x1900000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(base, (size + 4095) & ~4095)
        sections = opt + struct.unpack_from('<H', raw, nt + 20)[0]
        for i in range(struct.unpack_from('<H', raw, nt + 6)[0]):
            _, rva, length, offset = struct.unpack_from('<4I', raw, sections + 40 * i + 8)
            self.u.mem_write(base + rva, raw[offset:offset + length])

        def read(name):
            path = evidence / name
            self.inputs.append(identity(path))
            return json.loads(path.read_text('utf8'))

        ranges = read('particle-manager-boundaries.json')['ranges']
        listings = (read('particle-manager-narrow-complete-ida.json')['result'] +
                    read('particle-extra-narrow-complete-ida.json')['result'])
        assert len(ranges) == len(listings) == 19
        self.allowed, self.starts, self.windows = {}, 0, 0
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        for expected, actual in zip(ranges, listings):
            lo, hi = int(expected['start'], 16), int(expected['endExclusive'], 16)
            code = bytes(self.u.mem_read(lo, hi - lo))
            assert hashlib.sha256(code).hexdigest() == expected['sha256']
            decoded = [(x.address, x.size) for x in decoder.disasm(code, lo)]
            wanted = [(int(x['addr'], 16), x['size']) for x in expected['items']]
            assert decoded == wanted and decoded[-1][0] + decoded[-1][1] == hi
            assert [x[0] for x in decoded] == [int(x['addr'], 16) for x in actual['hits']]
            assert actual['n'] == len(wanted) and actual['cursor']['done']
            self.allowed.update(decoded)
            self.starts += len(decoded)
        for at, value in raw_rows(read('particle-manager-raw-ida.json')):
            assert bytes(self.u.mem_read(at, len(value))) == value, hex(at)
            self.windows += 1
        self.u.mem_map(0x500000, 0x10000)
        self.u.mem_map(0x600000, 0x900000)
        self.u.mem_map(0, 4096)  # Controlled empty x86 SEH chain for pool prologue.
        self.callbacks = {0x501000: 'time', 0x501020: 'qpf', 0x501040: 'qpc',
                          0x501060: 'qsort', 0x501080: 'visible', 0x5010a0: 'update',
                          0x5010c0: 'retire', 0x5010e0: 'draw', 0x501100: 'local',
                          0x2da8819: 'memset', 0x2d5bce0: 'int64double'}
        for slot, target in ((0x3ae8e84, 0x501000), (0x2f7801c, 0x501020),
                             (0x2f78018, 0x501040), (0x2f78828, 0x501060),
                             (0x3ae8e78, 0x501100)):
            self.word(slot, target)
        # The controlled GetClientTime callback returns through actual x87 ABI.
        self.u.mem_write(0x501000, b'\xd9\x05' + struct.pack('<I', 0x502000) + b'\xc3')
        for offset, target in ((0, 0x5010a0), (4, 0x501080), (8, 0x5010e0), (0x30, 0x5010c0)):
            self.word(0x503000 + offset, target)
        self.executed = set()
        self.u.hook_add(UC_HOOK_CODE, self.hook)
        self.configure(0)

    def word(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def integer(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def floats(self, address, count=1):
        return list(struct.unpack('<' + 'f' * count, self.u.mem_read(address, 4 * count)))

    def configure(self, previous):
        self.events, self.clock_at, self.timer, self.moves, self.created = [], 0, 0, {}, []
        self.clocks = [previous]
        for at, value in ((0x3634974, MANAGER), (MANAGER + 0x10, 0),
                          (MANAGER + 0x28, DRAW), (0x363496c, 10000),
                          (0x36349f0, VECTOR), (0x36349f4, VECTOR + 40000),
                          (0x36349f8, VECTOR + 40000), (0x36349fc, 0), (0x3634a00, 0),
                          (0x3634a0c, 0), (0x3634a10, 0), (0x340e650, 1),
                          (0x3ae8ef8, 0), (0x3634978, bits(previous))):
            self.word(at, value)
        nodes = bytearray(160000)
        vector = bytearray(40000)
        for i in range(10000):
            struct.pack_into('<4I', nodes, 16 * i, 0, PAYLOAD + i * 0x200,
                             NODES + (i - 1) * 16 if i else 0,
                             NODES + (i + 1) * 16 if i < 9999 else 0)
            struct.pack_into('<I', vector, 4 * i, NODES + i * 16)
        self.u.mem_write(NODES, bytes(nodes))
        self.u.mem_write(VECTOR, bytes(vector))
        self.word(0x36349e8, NODES + 9999 * 16)
        self.word(0x36349ec, NODES)

    def ret(self, cleanup=0, eax=None):
        sp = self.u.reg_read(UC_X86_REG_ESP)
        address = self.integer(sp)
        if eax is not None:
            self.u.reg_write(UC_X86_REG_EAX, eax & 0xffffffff)
        self.u.reg_write(UC_X86_REG_ESP, sp + 4 + cleanup)
        self.u.reg_write(UC_X86_REG_EIP, address)

    def call(self, address, args=()):
        self.u.mem_write(STACK, struct.pack('<' + 'I' * (1 + len(args)), STOP, *args))
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.u.reg_write(UC_X86_REG_ECX, MANAGER)
        self.u.emu_start(address, STOP, count=2000000)
        assert self.u.reg_read(UC_X86_REG_EIP) == STOP, 'Original did not return'
        return self.u.reg_read(UC_X86_REG_EAX)

    def hook(self, u, address, size, _):
        if address == 0x501006:
            return
        kind = self.callbacks.get(address)
        if kind is None:
            assert self.allowed.get(address) == size, 'Unverified instruction ' + hex(address)
            self.executed.add(address)
            return
        sp = u.reg_read(UC_X86_REG_ESP)
        if kind == 'time':
            sample = self.clocks[min(self.clock_at, len(self.clocks) - 1)]
            self.clock_at += 1
            self.events.append([0, bits(sample)])
            u.mem_write(0x502000, struct.pack('<f', sample))
        elif kind in ('qpf', 'qpc'):
            value = 1000000 if kind == 'qpf' else 1000 + self.timer
            self.timer += kind == 'qpc'
            u.mem_write(self.integer(sp + 4), struct.pack('<q', value))
            self.ret(4, 1)
        elif kind == 'int64double':
            value = (u.reg_read(UC_X86_REG_EDX) << 32) | u.reg_read(UC_X86_REG_ECX)
            u.reg_write(UC_X86_REG_XMM0, int.from_bytes(struct.pack('<d', float(value)) + bytes(8), 'little'))
            self.ret()
        elif kind == 'memset':
            target, value, count = struct.unpack('<3I', u.mem_read(sp + 4, 12))
            assert count <= 40000
            u.mem_write(target, bytes([value & 255]) * count)
            self.ret(eax=target)
        elif kind == 'qsort':
            target, count, width, comparator = struct.unpack('<4I', u.mem_read(sp + 4, 16))
            assert width == 4 and comparator == 0x1de42d0
            values = list(struct.unpack('<' + 'I' * count, u.mem_read(target, 4 * count)))
            values.sort(key=lambda p: self.floats(p + 0x18)[0], reverse=True)
            if values:
                u.mem_write(target, struct.pack('<' + 'I' * count, *values))
            self.ret()
        elif kind == 'local':
            self.events.append([2])
            self.ret(eax=ENTITY)
        else:
            obj = u.reg_read(UC_X86_REG_ECX)
            identifier = self.integer(obj + 0x158)
            if kind == 'visible':
                self.events.append([1, identifier])
                self.ret(eax=self.integer(obj + 0x154))
            elif kind == 'update':
                self.events.append([3, identifier, self.integer(sp + 4)])
                move, change_die, after_die = self.moves[obj]
                origin = self.floats(obj + 0x94, 3)
                u.mem_write(obj + 0x94, struct.pack('<3f', *(f32(x + y) for x, y in zip(origin, move))))
                if change_die:
                    u.mem_write(obj + 0x70, struct.pack('<f', after_die))
                self.ret(4)
            elif kind in ('retire', 'draw'):
                self.events.append([4 if kind == 'retire' else 5, identifier])
                self.ret()
            else:
                raise AssertionError(kind)

    def frame(self, case):
        self.configure(case['previous'])
        self.clocks = case['clocks']
        self.u.mem_write(ENTITY + 0xc7c, struct.pack('<3f', *case['camera']))
        for row in case['particles']:
            obj = self.call(0x1de4d40, (0x1e0,))
            assert obj != 0
            self.created.append(obj)
            self.word(obj, 0x503000)
            self.word(obj + 0x154, row['visible'])
            self.word(obj + 0x158, row['id'])
            self.u.mem_write(obj + 0x70, struct.pack('<f', row['die']))
            self.u.mem_write(obj + 0x94, struct.pack('<3f', *row['origin']))
            self.moves[obj] = row['move'], row['changeDie'], row['afterDie']
        self.call(0x1de4510)
        active = []
        at = self.integer(0x36349fc)
        while at:
            active.append(self.integer(self.integer(at + 4) + 0x158))
            at = self.integer(at + 8)
        particles = [[self.integer(obj + 0x158), self.integer(obj + 0x70),
                      *(self.integer(obj + 0x94 + 4 * axis) for axis in range(3))]
                     for obj in self.created]
        return dict(events=self.events, active=active, count=self.integer(MANAGER + 0x10),
                    drawCount=self.integer(MANAGER + 0xc), previous=self.integer(0x3634978),
                    particles=particles)

    def camera(self, previous, sample, gravity, camera):
        self.configure(previous)
        self.clocks = [sample]
        self.u.mem_write(0x3634a24, struct.pack('<3f', 1, 2, 3))
        self.call(0x1de5050, tuple(bits(x) for x in (gravity, *camera)))
        return [self.integer(0x3c4d760), *(self.integer(0x3634a24 + 4 * i) for i in range(3)),
                self.integer(0x3634978)]

    def compare(self, left, right):
        self.word(0x504000, PAYLOAD)
        self.word(0x504004, PAYLOAD + 0x200)
        self.word(PAYLOAD + 0x18, left)
        self.word(PAYLOAD + 0x218, right)
        value = self.call(0x1de42d0, (0x504000, 0x504004))
        return struct.unpack('<i', struct.pack('<I', value))[0]
