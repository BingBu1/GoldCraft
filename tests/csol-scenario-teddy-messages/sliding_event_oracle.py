#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original Teddy PushEnter and owner TempEmit writer with explicit ABI fixtures.

Original x86 instructions execute unchanged. Engine-supplied callback meanings,
message serialization and timer Add are explicit fixtures, not original HW or
original timer internals. No original DLL is loaded into a native process.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

MP_SHA = '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'


def identity(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def windows(value):
    if isinstance(value, dict):
        if isinstance(value.get('addr'), str) and isinstance(value.get('data'), str):
            yield value
        else:
            for item in value.values():
                yield from windows(item)
    elif isinstance(value, list):
        for item in value:
            yield from windows(item)


class Original:
    def __init__(self, sample, evidence):
        self.inputs = [identity(sample), identity(Path(__file__))]
        assert self.inputs[0]['sha256'] == MP_SHA
        raw = sample.read_bytes()
        nt = struct.unpack_from('<I', raw, 60)[0]
        assert raw[nt:nt + 4] == b'PE\0\0' and struct.unpack_from('<H', raw, nt + 4)[0] == 0x14c
        optional = nt + 24
        base, = struct.unpack_from('<I', raw, optional + 28)
        image_size, = struct.unpack_from('<I', raw, optional + 56)
        assert base == 0x10000000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(base, (image_size + 4095) & ~4095)
        sections = optional + struct.unpack_from('<H', raw, nt + 20)[0]
        for index in range(struct.unpack_from('<H', raw, nt + 6)[0]):
            _, rva, length, offset = struct.unpack_from('<4I', raw, sections + index * 40 + 8)
            self.u.mem_write(base + rva, raw[offset:offset + length])
        self.verified = []
        names = ('initial-ida.json', 'callee-raw.json', 'push-config-raw.json', 'push-enter-raw.json',
                 'owner-writer-raw.json', 'owner-abi-raw.json')
        for name in names:
            path = evidence / name
            self.inputs.append(identity(path))
            for row in windows(json.loads(path.read_text('utf8'))):
                address = int(row['addr'], 16)
                data = bytes(int(b, 16) for b in row['data'].split())
                assert bytes(self.u.mem_read(address, len(data))) == data, (name, hex(address))
                self.verified.append(dict(evidence=name, va=hex(address), bytes=len(data),
                                          sha256=hashlib.sha256(data).hexdigest()))
        assert len(self.verified) == 9
        self.allowed = {}
        for name in ('push-boundaries.json', 'writer-boundaries.json', 'owner-abi-ida.json', 'writer-defined-ida.json'):
            path = evidence / name
            self.inputs.append(identity(path))
            value = json.loads(path.read_text('utf8'))
            if name == 'push-boundaries.json':
                items = next(row['items'] for row in value if int(row['start'], 16) == 0x10936d30)
            elif name == 'writer-boundaries.json':
                items = value['items']
                assert len(items) == 97
            elif name == 'owner-abi-ida.json':
                lines = value['asm']['lines']
                assert value['instruction_count'] == value['total_instructions'] == len(lines) == 14
                instructions = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(bytes(self.u.mem_read(0x114b22b0, 34)), 0x114b22b0))
                assert [x.address for x in instructions] == [int(row['addr'], 16) for row in lines]
                items = [dict(addr=hex(x.address), size=x.size) for x in instructions]
            else:
                assert value['writer']['instruction_count'] == value['writer']['total_instructions'] == 97
                assert value['writer']['cursor']['done']
                assert [int(x['addr'], 16) for x in value['writer']['asm']['lines']] == [a for a in self.allowed if 0x11493570 <= a < 0x11493726]
                continue
            for item in items:
                address, size = int(item['addr'], 16), item['size']
                decoded = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(bytes(self.u.mem_read(address, size)), address))
                assert len(decoded) == 1 and decoded[0].size == size
                self.allowed[address] = size
        self.u.mem_map(0x500000, 0x10000)
        self.u.mem_map(0x600000, 0x10000)
        self.callbacks = {0x501000: 'owner', 0x501010: 'begin', 0x501020: 'end', 0x501030: 'string',
                          0x501040: 'short', 0x501050: 'coord', 0x501060: 'float', 0x501070: 'long',
                          0x501080: 'byte', 0x10936150: 'timerAdd'}
        self.executed = set()
        self.u.hook_add(UC_HOOK_CODE, self.hook)
        # Execute original GiveFnptrsToDll with a fully controlled 213-dword input.
        table = [0x510000 + i * 16 for i in range(213)]
        for slot, callback in ((0x122786d0, 0x501000), (0x12278664, 0x501010),
                               (0x12278668, 0x501020), (0x1227868c, 0x501030),
                               (0x12278674, 0x501040), (0x12278688, 0x501050),
                               (0x12278680, 0x501060), (0x12278678, 0x501070),
                               (0x1227866c, 0x501080)):
            table[(slot - 0x122785a0) // 4] = callback
        packed = struct.pack('<213I', *table)
        self.u.mem_write(0x607000, packed)
        self.u.mem_write(0x608000, struct.pack('<3I', 0x500000, 0x607000, 0x606000))
        self.u.reg_write(UC_X86_REG_ESP, 0x608000)
        self.u.emu_start(0x114b22b0, 0x500000, count=1000)
        assert bytes(self.u.mem_read(0x122785a0, len(packed))) == packed
        assert self.dword(0x122788f4) == 0x606000 and self.u.reg_read(UC_X86_REG_ESP) == 0x60800c
        self.table_sha = hashlib.sha256(packed).hexdigest()

    def dword(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def word(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value))

    def hook(self, u, address, size, _):
        if address not in self.callbacks:
            assert self.allowed.get(address) == size, ('Unverified instruction', hex(address), size)
            self.executed.add(address)
            return
        op = self.callbacks[address]
        sp = u.reg_read(UC_X86_REG_ESP)
        ret, arg = struct.unpack('<2I', u.mem_read(sp, 8))
        self.order.append(op)
        cleanup = 4
        if op == 'owner':
            assert arg == self.case['rawContainingEdict']
            u.reg_write(UC_X86_REG_EAX, self.case['owner'])
        elif op == 'begin':
            assert self.open is None
            route, message, origin, recipient = struct.unpack('<4I', u.mem_read(sp + 4, 16))
            assert (route, message, origin, recipient) == (0, 77, 0, 0)
            self.open = dict(route=route, message=message, recipient=recipient, pvs=None, fields=[], payload=bytearray())
        elif op == 'end':
            assert self.open is not None and self.dword(0x600014) == 0x42f60000
            assert [self.u.mem_read(node + 8, 1)[0] for node in self.nodes] == [0] * len(self.nodes)
            self.open['payload'] = self.open['payload'].hex()
            self.packets.append(self.open)
            self.open = None
        elif op == 'timerAdd':
            assert u.reg_read(UC_X86_REG_ECX) == 0x60000c
            assert self.dword(0x600014) == 0xbf800000 and self.open is None
            assert [self.u.mem_read(node + 8, 1)[0] for node in self.nodes] == [1] * len(self.nodes)
            duration, mode = struct.unpack('<fI', u.mem_read(sp + 4, 8))
            assert duration == self.case['duration'] and mode == 0
            self.timer = dict(duration=duration, mode=mode, startBefore=hex(self.dword(0x600014)), nodesInvalidated=len(self.nodes))
            cleanup = 12  # Explicit thiscall fixture; original timer internals are not executed.
        else:
            assert self.open is not None
            field = dict(operation=op, raw=arg)
            if op == 'string':
                raw = bytes(u.mem_read(arg, 260)).split(b'\0', 1)[0]
                field['value'] = raw.decode('ascii')
                encoded = raw + b'\0'
            elif op in ('coord', 'float'):
                value = struct.unpack('<f', struct.pack('<I', arg))[0]
                field['value'] = value
                encoded = struct.pack('<H', int(value * 8) & 65535) if op == 'coord' else struct.pack('<I', arg)
            elif op == 'byte':
                encoded = bytes([arg & 255])
            elif op == 'short':
                encoded = struct.pack('<H', arg & 65535)
            else:
                encoded = struct.pack('<I', arg)
            self.open['fields'].append(field)
            self.open['payload'] += encoded
        u.reg_write(UC_X86_REG_ESP, sp + cleanup)
        u.reg_write(UC_X86_REG_EIP, ret)

    def run(self, case):
        self.case, self.open, self.packets, self.order = case, None, [], []
        self.nodes = [0x605100 + i * 32 for i in range(case['nodes'])]
        self.u.mem_write(0x600000, bytes(256))
        for address, value in ((0x600004, 0x601000), (0x60101c, 0x602000), (0x602008, 0x603000),
                               (0x603238, case['rawContainingEdict']), (0x600018, 0x605000), (0x122775fc, 77)):
            self.word(address, value)
        self.word(0x600014, 0x42f60000)  # Existing timer start = 123.0f.
        self.u.mem_write(0x60002c, struct.pack('<f', case['duration']))
        self.u.mem_write(0x600034, bytes([case['effect']]))
        self.u.mem_write(0x12278584, struct.pack('<3f', *case['vectorGlobal']))
        self.u.mem_write(0x606000, struct.pack('<f', case['time']))
        for index, node in enumerate([0x605000] + self.nodes):
            following = (self.nodes + [0x605000])[index]
            self.u.mem_write(node, struct.pack('<3I', following, 0, 0))
        self.u.mem_write(0x608000, struct.pack('<I', 0x500000))
        self.u.reg_write(UC_X86_REG_ESP, 0x608000)
        self.u.reg_write(UC_X86_REG_ECX, 0x600000)
        self.u.emu_start(0x10936d30, 0x500000, count=10000)
        assert self.open is None and self.u.reg_read(UC_X86_REG_ESP) == 0x608004
        assert len(self.packets) == case['effect']
        assert self.order[-1] == 'timerAdd'
        assert self.dword(0x600014) == struct.unpack('<I', struct.pack('<f', case['time']))[0]
        if case['effect']:
            assert self.order[:3] == ['owner', 'begin', 'string'] and self.order[-2] == 'end'
            fields = self.packets[0]['fields']
            assert fields[0]['value'] == 'sprites/ef_bat.spr'
            assert fields[1]['raw'] == case['owner']
            assert [field['value'] for field in fields if field['operation'] == 'coord'] == list(case['vectorGlobal']) * 2
            assert len(bytes.fromhex(self.packets[0]['payload'])) == 82
        else:
            assert self.order == ['timerAdd']
        return dict(inputs=case, callbacks=self.order, timer=self.timer, finalStartRaw=hex(self.dword(0x600014)), packets=self.packets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('mp', 'evidence', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    assert not args.output.exists(), 'Use a fresh output directory'
    original = Original(args.mp, args.evidence)
    cases = []
    choices = itertools.product((0, 1), (1, 32, 256, 32767), ((0., 0., 0.), (1.1875, -2.1875, 3.0625), (-4096.125, 4096.125, -0.0625)), (0, 3), (0, 1))
    for effect, owner, vector, nodes, variant in choices:
        cases.append(original.run(dict(effect=effect, owner=owner, vectorGlobal=vector, nodes=nodes,
                                       duration=(.25, 7.5)[variant], time=(0., 12345.5)[variant], rawContainingEdict=0x650000 + owner * 4)))
    assert all(identity(Path(row['path'])) == row for row in original.inputs)
    args.output.mkdir(parents=True)
    fixture = args.output / 'sliding_event_cases.txt'
    lines = []
    for case in cases:
        inp = case['inputs']
        lines.append(' '.join(map(str, [inp['effect'], inp['owner'], *inp['vectorGlobal'], inp['duration'], inp['time'], inp['nodes'],
                                        case['packets'][0]['payload'] if case['packets'] else '-'])))
    fixture.write_text(str(len(lines)) + '\n' + '\n'.join(lines) + '\n', 'ascii')
    result = dict(success=True, inputsUnchanged=True, inputs=original.inputs, windows=original.verified, cases=cases,
                  events=len(cases), packets=sum(len(case['packets']) for case in cases), fixture=identity(fixture),
                  suppliedEngineTableDwords=213, suppliedEngineTableSha256=original.table_sha,
                  executedOriginalInstructions=[hex(a) for a in sorted(original.executed)],
                  boundaries=dict(owner='engine supplied callback(raw containing-edict) -> wire owner; semantic name not proven by matching HW',
                                  timerAdd='explicit thiscall fixture: duration/mode/order only; internals not executed',
                                  engine='registered id77 and little-endian field callback fixtures; coord truncates 1/8 low16',
                                  originalClientExecuted=False, deployed=False, liveGameplay=False, performanceMeasured=False))
    (args.output / 'result.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(json.dumps(dict(success=True, events=result['events'], packets=result['packets'], originalInstructions=len(original.executed))))


if __name__ == '__main__':
    main()
