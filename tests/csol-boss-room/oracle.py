#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare actual SDK room results with the identified original MP instructions.

Only SetModel and the imported atoi are controlled ABI boundaries. This does
not emulate an engine BSP loader, a BOSS, or a live client/server session.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

EXPECTED = '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'
ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = path.resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def result(path):
    value = json.loads(path.read_text('utf8'))
    value = value.get('response', value).get('result', value.get('response', value))
    assert not value.get('isError'), path
    return value.get('structuredContent', value)


class Original:
    entity, pev, data, strings, globals, edict = 0x600000, 0x610000, 0x620000, 0x630000, 0x640000, 0x650000
    stack, stop, set_model, atoi = 0x660ff0, 0x500300, 0x500100, 0x500200

    def __init__(self, module, evidence):
        blob = module.read_bytes()
        assert hashlib.sha256(blob).hexdigest() == EXPECTED
        pe = struct.unpack_from('<I', blob, 60)[0]
        assert struct.unpack_from('<H', blob, pe + 4)[0] == 0x14c
        optional = pe + 24
        image_base, = struct.unpack_from('<I', blob, optional + 28)
        assert image_base == 0x10000000
        image_size, = struct.unpack_from('<I', blob, optional + 56)
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(image_base, (image_size + 4095) & ~4095)
        table = optional + struct.unpack_from('<H', blob, pe + 20)[0]
        for i in range(struct.unpack_from('<H', blob, pe + 6)[0]):
            _, va, size, offset = struct.unpack_from('<4I', blob, table + i * 40 + 8)
            if size:
                self.u.mem_write(image_base + va, blob[offset:offset + size])
        self.u.mem_map(0x500000, 0x200000)
        self.allowed = {}
        self.inputs = [identity(module), identity(Path(__file__))]
        for label in ('room-keyvalue', 'room-spawn', 'room-get', 'room-select', 'cache-constructor'):
            auxiliary_path = evidence / (label + '-aux.json')
            auxiliary = json.loads(auxiliary_path.read_text('utf8'))
            assert auxiliary['sha256'] == EXPECTED
            lo, hi = int(auxiliary['start'], 16), int(auxiliary['endExclusive'], 16)
            code = bytes(self.u.mem_read(lo, hi - lo))
            decoded = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(code, lo))
            assert sum(row.size for row in decoded) == hi - lo
            assert [(row.address, row.bytes.hex()) for row in decoded] == [
                (int(row['addr'], 16), row['bytes']) for row in auxiliary['rows']]
            self.allowed.update({row.address: row.size for row in decoded})
            chunks_path = evidence / (label + '-chunks.json')
            addresses = []
            for chunk in json.loads(chunks_path.read_text('utf8')):
                path = evidence / (chunk['label'] + '.json')
                captured = result(path)['hits']
                assert len(captured) == chunk['count']
                addresses.extend(int(row['addr'], 16) for row in captured)
                self.inputs.append(identity(path))
            assert addresses == [row.address for row in decoded]
            raw_path = evidence / (label + '-raw.json')
            windows = result(raw_path)['result']
            assert len(windows) == 1 and int(windows[0]['addr'], 16) == lo
            assert bytes(int(value, 16) for value in windows[0]['data'].split()) == code
            self.inputs += [identity(auxiliary_path), identity(chunks_path), identity(raw_path)]
        assert bytes(self.u.mem_read(0x117a552c, 3)) == b'id\0'
        self.word(self.entity + 8, self.pev)
        self.word(self.pev + 0x238, self.edict)
        self.word(0x122788f4, self.globals)
        self.word(self.globals + 0x98, self.strings)
        self.word(0x122785ac, self.set_model)
        self.word(0x117402bc, self.atoi)
        self.calls = []
        self.rooms = {}
        self.finds = []
        self.executed = set()
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def word(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def read(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def hook(self, u, address, size, _):
        if address == self.stop:
            u.emu_stop()
            return
        if address == 0x1152e590:
            sp = u.reg_read(UC_X86_REG_ESP)
            previous, classname = struct.unpack('<2I', u.mem_read(sp + 4, 8))
            assert bytes(u.mem_read(classname, 10)) == b'boss_room\0'
            self.finds.append(previous)
            ordered = sorted(self.rooms)
            if previous:
                old_slot = (previous - 0x680000) // 0x200
                assert previous == 0x680000 + old_slot * 0x200
                ordered = [slot for slot in ordered if slot > old_slot]
            found = 0x680000 + ordered[0] * 0x200 if ordered else 0
            u.reg_write(UC_X86_REG_EAX, found)
            u.reg_write(UC_X86_REG_EIP, self.read(sp))
            u.reg_write(UC_X86_REG_ESP, sp + 4)
            return
        if address in (0x113eb3a0, 0x113eaf60):
            # Constructor's base/auxiliary initialization are explicit control
            # boundaries; the real caller still performs every cache store.
            sp = u.reg_read(UC_X86_REG_ESP)
            u.reg_write(UC_X86_REG_EAX, self.entity if address == 0x113eb3a0 else 0)
            u.reg_write(UC_X86_REG_EIP, self.read(sp))
            u.reg_write(UC_X86_REG_ESP, sp + (8 if address == 0x113eb3a0 else 4))
            return
        if address not in (self.set_model, self.atoi):
            assert self.allowed.get(address) == size, ('unverified instruction', hex(address))
            self.executed.add(address)
            return
        sp = u.reg_read(UC_X86_REG_ESP)
        if address == self.atoi:
            assert self.read(sp + 4) == self.strings + 0x300
            self.calls.append('atoi')
            u.reg_write(UC_X86_REG_EAX, self.atoi_result & 0xffffffff)
        else:
            assert self.read(sp + 4) == self.edict and self.read(sp + 8) == self.strings + 0x100
            assert bytes(u.mem_read(self.strings + 0x100, 4)) == b'*90\0'
            assert self.read(self.pev + 0x118) == self.read(self.pev + 0x11c) == 0
            assert self.read(self.pev + 0x128) == 0x21
            self.calls.append('SetModel')
            self.word(self.pev + 0xc4, 91)
            u.mem_write(self.pev + 0xec, struct.pack('<6f', -34, -590, 240, 958, 590, 496))
        u.reg_write(UC_X86_REG_EAX, 0 if address == self.set_model else self.atoi_result & 0xffffffff)
        u.reg_write(UC_X86_REG_EIP, self.read(sp))
        u.reg_write(UC_X86_REG_ESP, sp + 4)

    def call(self, address, argument=None):
        self.word(self.stack, self.stop)
        if argument is not None:
            self.word(self.stack + 4, argument)
        self.u.reg_write(UC_X86_REG_ECX, self.entity)
        self.u.reg_write(UC_X86_REG_ESP, self.stack)
        self.u.emu_start(address, 0, count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP) == self.stop
        assert self.u.reg_read(UC_X86_REG_ESP) == self.stack + (8 if argument is not None else 4)

    def key(self, name, text, value):
        self.u.mem_write(self.strings + 0x200, name.encode() + b'\0')
        self.u.mem_write(self.strings + 0x300, text.encode() + b'\0')
        self.u.mem_write(self.data, struct.pack('<4I', self.strings, self.strings + 0x200, self.strings + 0x300, 17))
        self.atoi_result = value
        before = len(self.calls)
        self.call(0x107dce30, self.data)
        assert self.calls[before:] == (['atoi'] if name == 'id' else [])
        ident = struct.unpack('<i', self.u.mem_read(self.entity + 0xb0, 4))[0]
        return [ident, self.read(self.data + 12)]

    def spawn(self):
        self.word(self.pev + 0x118, 5)
        self.word(self.pev + 0x11c, 2)
        self.word(self.pev + 0x128, 0x21)
        self.word(self.pev + 0xc8, 0x100)
        self.u.mem_write(self.strings + 0x100, b'*90\0')
        self.u.mem_write(self.pev + 8, struct.pack('<3f', 100, 200, 300))
        before = len(self.calls)
        self.call(0x108f2260)
        assert self.calls[before:] == ['SetModel']
        return [self.read(self.pev + offset) for offset in (0x118, 0x11c, 0x128, 0xc4)] + [
            self.read(self.entity + 0xb0)] + list(struct.unpack('<3I', self.u.mem_read(self.pev + 8, 12))) + list(
            struct.unpack('<6I', self.u.mem_read(self.pev + 0xec, 24)))

    def cache_query(self, request, before):
        self.u.mem_write(self.data, struct.pack('<6I', *before))
        self.u.mem_write(self.stack, struct.pack('<4I', self.stop, self.data, self.data + 12, request & 0xffffffff))
        self.finds = []
        self.u.reg_write(UC_X86_REG_ESP, self.stack)
        self.u.emu_start(0x113e6d70, 0, count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP) == self.stop
        returned = self.u.reg_read(UC_X86_REG_EAX) & 255
        assert returned in (0, 1)
        return [0 if returned else 1, len(self.finds), *before,
                *struct.unpack('<6I', self.u.mem_read(self.data, 24))]

    def cache_log(self, path):
        self.word(0x11d9cde0, 0xffffffff)
        self.u.mem_write(0x11d9d518, bytes(24))
        checked, resets, records = 0, 0, []
        for line in path.read_text('utf8').splitlines():
            if line.startswith('ROOM_DATA '):
                slot, ident, *bounds = map(int, line.split()[1:])
                assert len(bounds) == 6 and 0 <= slot < 64
                entity, pev = 0x680000 + slot * 0x200, 0x6c0000 + slot * 0x100
                self.rooms[slot] = ident
                self.word(entity + 8, pev)
                self.word(entity + 0xb0, ident)
                self.u.mem_write(pev + 0xec, struct.pack('<6I', *bounds))
            elif line.startswith('ROOM_DELETE '):
                del self.rooms[int(line.split()[1])]
            elif line == 'ROOM_RESET':
                self.call(0x113e4940, 0)
                resets += 1
            elif line.startswith('ROOM_QUERY '):
                request, *actual = map(int, line.split()[1:])
                assert len(actual) == 14
                expected = self.cache_query(request, actual[2:8])
                assert expected == actual, dict(query=checked, requested=request, original=expected, sdk=actual)
                records.append(dict(request=request, output=actual))
                checked += 1
        assert checked > 0 and resets > 0
        self.inputs.append(identity(path))
        return dict(queries=checked, controllerResets=resets, records=records,
                    originalConstructorBoundary='base and auxiliary construction controlled; actual cache stores execute')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('module', 'evidence', 'sdk-log', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--cache-log', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    original = Original(args.module, args.evidence)
    key_cases = [('id', value, result) for value, result in (
        ('23', 23), ('-1', -1), ('', 0), ('unused', 0), ('  +17tail', 17),
        ('-2147483648', -2147483648), ('2147483647', 2147483647), ('0x20', 0), ('0', 0), ('37', 37))]
    key_cases += [(name, '999', 999) for name in ('ID', 'id ', 'target', 'model', 'origin')]
    expected_keys = [original.key(*case) for case in key_cases]
    expected_spawn = original.spawn()
    actual_keys, actual_spawns = [], []
    for line in args.sdk_log.read_text('utf8').splitlines():
        if line.startswith('ROOM_KEY '):
            number, *values = map(int, line.split()[1:])
            assert number == len(actual_keys)
            actual_keys.append(values)
        elif line.startswith('ROOM_SPAWN '):
            actual_spawns.append(list(map(int, line.split()[1:])))
    assert actual_keys == expected_keys, (actual_keys, expected_keys)
    assert actual_spawns == [expected_spawn], (actual_spawns, expected_spawn)
    cache = original.cache_log(args.cache_log) if args.cache_log else None
    original.inputs += [identity(args.sdk_log), identity(ROOT / 'tests/csol-boss-room/sdk.cpp'),
                        identity(ROOT / 'native/csol/server/scenario/core/scenario_boss_room.cpp'),
                        identity(ROOT / 'native/csol/server/scenario/core/scenario_boss_room.h')]
    unchanged = all(identity(Path(row['path'])) == row for row in original.inputs)
    report = dict(success=unchanged, inputsUnchanged=unchanged, originalSha256=EXPECTED,
                  keyCases=len(key_cases), spawnCases=1, idaInstructions=len(original.allowed),
                  executedInstructions=len(original.executed), imports='controlled atoi and SetModel ABI',
                  keys=actual_keys, spawn=expected_spawn, inputs=original.inputs,
                  cache=cache,
                  deployment=False, gameplay=False, performanceAccepted=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', 'utf8')
    print(json.dumps({key: report[key] for key in ('success', 'keyCases', 'spawnCases', 'idaInstructions', 'executedInstructions')}))
    return 0 if unchanged else 1


if __name__ == '__main__':
    raise SystemExit(main())
