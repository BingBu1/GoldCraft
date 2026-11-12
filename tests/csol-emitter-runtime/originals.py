# SPDX-License-Identifier: GPL-3.0-or-later
"""Unchanged x86 emitter instructions with controlled engine/factory boundaries.

The private client and complete IDA captures are required. No game DLL is
loaded into this Python process. This tests the common owner, not particles.
"""
import hashlib
import json
import struct

import capstone
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP

CLIENT_SHA = 'f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
MANAGER, MODEL, SOURCE, STACK, STOP = 0x600000, 0x658000, 0x650000, 0xeff000, 0x500000
RECORDS = MANAGER + 0xb414


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


class Original:
    def __init__(self, client, evidence):
        self.inputs = [identity(client)]
        assert self.inputs[0]['sha256'] == CLIENT_SHA
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

        ranges = read('boundaries.json')['ranges']
        self.allowed, self.starts, self.windows = {}, 0, 0
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        for item in ranges:
            lo, hi = int(item['start'], 16), int(item['endExclusive'], 16)
            code = bytes(self.u.mem_read(lo, hi - lo))
            assert hashlib.sha256(code).hexdigest() == item['sha256']
            decoded = [(x.address, x.size) for x in decoder.disasm(code, lo)]
            assert decoded == [(int(x['addr'], 16), x['size']) for x in item['items']]
            assert decoded[-1][0] + decoded[-1][1] == hi
            self.allowed.update(decoded)
        wanted = [int(i['addr'], 16) for r in ranges for i in r['items']]
        actual = []
        for index in range(11):
            listing = read(f'ida-{index}.json')
            chunk, result = listing['chunk'], listing['result']
            assert result['cursor']['done'] and result['n'] == len(result['hits']) == chunk['expected']
            actual += [int(h['addr'], 16) for h in result['hits']]
        assert actual == wanted
        self.starts = len(actual)
        for row in read('raw-ida.json')['result']:
            code = bytes(int(x, 16) for x in row['data'].split())
            assert bytes(self.u.mem_read(int(row['addr'], 16), len(code))) == code
            self.windows += 1
        reset = read('reset-complete-0.json')
        assert reset['n'] == len(reset['hits']) == 1 and int(reset['hits'][0]['addr'], 16) == 0x2802ff0
        reset_raw = read('reset-complete-1.json')['result'][0]
        assert bytes(int(x, 16) for x in reset_raw['data'].split()) == bytes(self.u.mem_read(0x2802ff0, 3)) == b'\xc2\0\0'
        self.allowed[0x2802ff0] = 3
        self.starts += 1
        self.windows += 1
        self.u.mem_map(0x500000, 0x10000)
        self.u.mem_map(0x600000, 0x900000)
        self.callbacks = {0x501000: 'time', 0x501020: 'load', 0x501040: 'model',
                          0x501060: 'console', 0x501080: 'random',
                          0x2ad2c30: 'factory', 0x2da8819: 'memset'}
        for slot, target in ((0x3ae8e84, 0x501000), (0x3ae8da8, 0x501020),
                             (0x3ae8ea4, 0x501040), (0x3ae8e50, 0x501060), (0x3ae8eb8, 0x501080)):
            self.word(slot, target)
        for at, data in ((0x501000, 0x502000), (0x501080, 0x502004)):
            self.u.mem_write(at, b'\xd9\x05' + struct.pack('<I', data) + b'\xc3')
        self.executed = set()
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def word(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def integer(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def text(self, address):
        return bytes(self.u.mem_read(address, 128)).split(b'\0')[0].decode('ascii')

    def identifier(self, record):
        text = self.text(record + 0x74)
        return int(text[3:]) if text else 0

    def ret(self, eax=None):
        sp = self.u.reg_read(UC_X86_REG_ESP)
        address = self.integer(sp)
        if eax is not None:
            self.u.reg_write(UC_X86_REG_EAX, eax & 0xffffffff)
        self.u.reg_write(UC_X86_REG_ESP, sp + 4)
        self.u.reg_write(UC_X86_REG_EIP, address)

    def call(self, address, args=()):
        self.u.mem_write(STACK, struct.pack('<' + 'I' * (1 + len(args)), STOP, *args))
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.u.reg_write(UC_X86_REG_ECX, MANAGER)
        self.u.emu_start(address, STOP, count=10000000)
        assert self.u.reg_read(UC_X86_REG_EIP) == STOP
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK + 4 + (4 if args else 0)

    def hook(self, u, address, size, _):
        if address in (0x501006, 0x501086):
            return  # Controlled callback's actual x87 return instruction.
        kind = self.callbacks.get(address)
        if kind is None:
            assert self.allowed.get(address) == size, 'Unverified instruction ' + hex(address)
            self.executed.add(address)
            if address == 0x2aa4a40:
                self.events.append([2, self.integer(0x3ae91d4)])
            return
        sp = u.reg_read(UC_X86_REG_ESP)
        if kind == 'time':
            self.events.append([4, bits(self.case['now'])])
            u.mem_write(0x502000, struct.pack('<f', self.case['now']))
        elif kind == 'load':
            path = self.integer(sp + 4)
            if self.mode == 'add':
                record = path - 0x74
                self.events.append([0, self.identifier(record), self.integer(record + 0x1c),
                                    int(self.integer(record + 0x114) != 0)])
                self.word(0x3ae91d4, bits(self.case['afterLoad']))
            else:
                self.video_paths.append(self.text(path))
            self.ret(2)
        elif kind == 'model':
            if self.mode == 'add':
                self.events.append([1, self.integer(sp + 4)])
            self.ret(MODEL if self.case['model'] else 0)
        elif kind == 'console':
            fmt, path = self.text(self.integer(sp + 4)), self.integer(sp + 8)
            if fmt.startswith('Overflow'):
                self.events.append([3, self.identifier(path - 0x74)])
            else:
                assert fmt == 'Bad sprite in particle emitter: %s\n'
                record = path - 0x74
                self.events.append([5, self.identifier(record), self.integer(record + 0x1c),
                                    self.integer(record + 0x28)])
            self.ret()
        elif kind == 'factory':
            record = u.reg_read(UC_X86_REG_EDI) - 0x44
            self.events.append([7, self.identifier(record), bits(self.case['now']),
                                self.integer(record + 0x1c), self.integer(record + 0x28)])
            failed = self.created == self.case['failAt']
            self.created += 1
            obj = 0x800000 + self.created * 0x200
            if not failed:
                u.mem_write(obj, bytes(0x1e0))
            self.ret(0 if failed else obj)
        elif kind == 'random':
            lo, hi = struct.unpack('<2f', u.mem_read(sp + 4, 8))
            u.mem_write(0x502004, struct.pack('<f', (lo + hi) * .5))
        elif kind == 'memset':
            target, value, count = struct.unpack('<3I', u.mem_read(sp + 4, 12))
            assert count == 0xb400
            u.mem_write(target, bytes([value & 255]) * count)
            self.ret(target)
        else:
            raise AssertionError(kind)

    def seed(self, address, row):
        self.u.mem_write(address, bytes(0x168))
        for offset, name in ((0, 'tag'), (0x10, 'total'), (0x14, 'burst'),
                             (0x1c, 'remaining'), (0x140, 'active'), (0x15c, 'kind'), (0x160, 'retained')):
            self.word(address + offset, row[name])
        for offset, name in ((0x24, 'interval'), (0x28, 'next')):
            self.word(address + offset, bits(row[name]))
        self.word(address + 0x114, MODEL if row['model'] else 0)
        self.word(address + 0x2c, (0x50, 0x30, 0x34)[row['kind'] % 3])
        self.u.mem_write(address + 0x74, f"id:{row['id']}".encode() + b'\0')

    def run(self, mode, case):
        self.case, self.mode = case, mode
        self.events, self.video_paths, self.created = [], [], 0
        self.u.mem_write(MANAGER, bytes(0x16840))
        self.word(MODEL + 0x8c, 12)
        self.word(0x3ae91d4, bits(case['cached']))
        for index, row in enumerate(case['records']):
            self.seed(RECORDS + index * 0x168, row)
        if mode == 'add':
            self.seed(SOURCE, case['input'])
            self.call(0x2aa49b0, (SOURCE,))
        elif mode == 'frame':
            self.call(0x2aa6650)
        elif mode == 'video':
            self.call(0x2aa7020)
            assert self.video_paths == ['sprites/wsplash3.spr', 'sprites/effects/ripple.spr',
                                        'sprites/burn5.spr', 'sprites/burn6.spr', 'sprites/burn7.spr']
        elif mode == 'reset':
            self.call(0x2802ff0)
        else:
            raise AssertionError(mode)
        rows = []
        for index in range(128):
            at = RECORDS + index * 0x168
            rows.append([*(self.integer(at + o) for o in (0, 0x10, 0x14, 0x1c, 0x140, 0x24, 0x28)),
                         int(self.integer(at + 0x114) != 0), self.identifier(at),
                         self.integer(at + 0x15c), self.integer(at + 0x160)])
        return dict(events=self.events, records=rows)
