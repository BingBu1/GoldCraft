#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original Teddy Meteor enter/burn/stop instructions with ABI fixtures.

The original MP instructions and writers execute unchanged. Engine RNG, edict
index, BaseEnter/BaseEvent/timer and a failed boss_room lookup are controlled
boundaries. No native DLL, original client renderer or live game is executed.
"""
import argparse
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import struct
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('sliding_original', Path(__file__).with_name('sliding_event_oracle.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def unwrap(value):
    while isinstance(value, dict) and 'content' in value:
        value = json.loads(next(x['text'] for x in value['content'] if x['type'] == 'text'))
    return value


class Original(module.Original):
    def __init__(self, sample, sliding_evidence, evidence):
        super().__init__(sample, sliding_evidence)
        self.meteor_windows = []
        for name in ('event-raw-ida.json', 'fresh-hand-enter-stop-raw-ida.json', 'attached-writer-raw-ida.json'):
            path = evidence / name
            self.inputs.append(module.identity(path))
            for row in module.windows(json.loads(path.read_text('utf8'))):
                start = int(row['addr'], 16)
                data = bytes(int(x, 16) for x in row['data'].split())
                assert bytes(self.u.mem_read(start, len(data))) == data, (name, hex(start))
                self.meteor_windows.append(dict(va=hex(start), bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        bounds = json.loads((evidence / 'event-boundaries.json').read_text())
        self.inputs.append(module.identity(evidence / 'event-boundaries.json'))
        event_capture = unwrap(json.loads((evidence / 'event-complete-ida.json').read_text()))['result']
        assert len(event_capture) == 1
        self.add_range(bounds, event_capture[0])
        self.inputs.append(module.identity(evidence / 'event-complete-ida.json'))
        bounds = json.loads((evidence / 'auxiliary-boundaries.json').read_text())
        captures = unwrap(json.loads((evidence / 'auxiliary-complete-ida.json').read_text()))
        for region, capture in zip(bounds['ranges'], captures, strict=True):
            self.add_range(region, capture)
        self.inputs += [module.identity(evidence / name) for name in ('auxiliary-boundaries.json', 'auxiliary-complete-ida.json')]
        self.add_range(json.loads((evidence / 'attached-writer-boundaries.json').read_text()),
                       unwrap(json.loads((evidence / 'attached-writer-complete-ida.json').read_text())))
        self.inputs += [module.identity(evidence / name) for name in ('attached-writer-boundaries.json', 'attached-writer-complete-ida.json')]
        assert bytes(self.u.mem_read(0x117d4d34, 18)) == b'sprites/burn5.spr\0'
        self.callbacks.update({0x501090: 'randomfloat', 0x5010a0: 'randomlong', 0x1093b200: 'enterBase',
                               0x1093cf40: 'base', 0x113e6d70: 'room'})
        self.word(0x12278720, 0x501090)
        self.word(0x1227871c, 0x5010a0)
        self.u.mem_write(0x501090, b'\xd9\x05' + struct.pack('<I', 0x502000) + b'\xc3')
        self.inputs.append(module.identity(Path(__file__)))

    def add_range(self, region, capture):
        items = region.get('instructions', region.get('items'))
        start = int(region['start'], 16)
        end = int(region.get('endExclusive', region.get('end')), 16)
        lines = capture.get('asm', {}).get('lines', capture.get('matches', []))
        assert capture['cursor']['done'] and not capture.get('truncated')
        assert len(lines) == len(items)
        assert [int(x['addr'], 16) for x in lines] == [int(x['addr'], 16) for x in items]
        decoded = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(bytes(self.u.mem_read(start, end - start)), start))
        assert [(x.address, x.size) for x in decoded] == [(int(x['addr'], 16), x['size']) for x in items]
        assert decoded[-1].address + decoded[-1].size == end
        self.allowed.update({int(x['addr'], 16): x['size'] for x in items})

    def hook(self, u, address, size, _):
        if address == 0x11493000:
            sp = u.reg_read(UC_X86_REG_ESP)
            self.writer_args.append(list(struct.unpack('<26I', u.mem_read(sp + 4, 104))))
        if address not in self.callbacks:
            if 0x501090 < address < 0x501097:
                return
            assert self.allowed.get(address) == size, ('Unverified instruction', hex(address), size)
            self.executed.add(address)
            return
        op = self.callbacks[address]
        sp = u.reg_read(UC_X86_REG_ESP)
        ret, arg = struct.unpack('<2I', u.mem_read(sp, 8))
        self.order.append(op)
        cleanup = 4
        if op == 'randomfloat':
            low, high = struct.unpack('<2f', u.mem_read(sp + 4, 8))
            assert (low, high) == (2., 5.)
            self.u.mem_write(0x502000, struct.pack('<f', self.case['float'][self.float_index]))
            self.float_index += 1
            return  # Actual fld/ret stub provides the engine x87 float ABI.
        if op == 'randomlong':
            low, high = struct.unpack('<2i', u.mem_read(sp + 4, 8))
            index = self.long_index
            assert (low, high) == ((20, 23) if index % 2 == 0 else (12, 32))
            self.long_index += 1
            u.reg_write(UC_X86_REG_EAX, self.case['long'][index])
        elif op == 'owner':
            index = self.owner_index
            assert arg == self.case['rawEdicts'][index]
            u.reg_write(UC_X86_REG_EAX, self.case['owners'][index])
            self.owner_index += 1
        elif op == 'enterBase':
            assert u.reg_read(UC_X86_REG_ECX) == 0x600000
        elif op == 'timerAdd':
            assert u.reg_read(UC_X86_REG_ECX) == 0x60000c
            duration, event = struct.unpack('<fI', u.mem_read(sp + 4, 8))
            assert (duration, event) == (struct.unpack('<f', struct.pack('<I', 0x40833333))[0], 2)
            cleanup = 12
        elif op == 'base':
            assert u.reg_read(UC_X86_REG_ECX) == 0x600000 and arg == self.case['event'] & 0xffffffff
            cleanup = 8
        elif op == 'room':
            low, high, tail = struct.unpack('<3I', u.mem_read(sp + 4, 12))
            assert tail == 0
            self.room_outputs = [low, high]
            u.reg_write(UC_X86_REG_EAX, 0)
        elif op == 'begin':
            assert self.open is None
            route, message, origin, recipient = struct.unpack('<4I', u.mem_read(sp + 4, 16))
            assert (route, message, origin, recipient) == (0, 77, 0, 0)
            self.open = dict(route=route, message=message, recipient=recipient, pvs=None, fields=[], payload=bytearray())
        elif op == 'end':
            assert self.open is not None
            self.open['payload'] = self.open['payload'].hex()
            self.packets.append(self.open)
            self.open = None
            # Reacquire the second host/edict after the first complete emission.
            if len(self.packets) == 1 and self.case['kind'] == 'enter':
                self.word(0x60101c, 0x603000)
        else:
            assert self.open is not None
            field = dict(operation=op, raw=arg)
            if op == 'string':
                raw = bytes(u.mem_read(arg, 260)).split(b'\0', 1)[0]
                field['value'] = raw.decode('ascii')
                encoded = raw + b'\0'
            elif op in ('float', 'coord'):
                field['value'] = struct.unpack('<f', struct.pack('<I', arg))[0]
                encoded = struct.pack('<I', arg) if op == 'float' else struct.pack('<H', int(field['value'] * 8) & 65535)
            else:
                encoded = struct.pack({"byte": '<B', "short": '<H', "long": '<I'}[op], arg & {"byte": 255, "short": 65535, "long": 0xffffffff}[op])
            self.open['fields'].append(field)
            self.open['payload'] += encoded
        u.reg_write(UC_X86_REG_ESP, sp + cleanup)
        u.reg_write(UC_X86_REG_EIP, ret)

    def run(self, case):
        self.case, self.open, self.packets, self.order, self.writer_args = case, None, [], [], []
        self.float_index = self.long_index = self.owner_index = 0
        self.room_outputs = None
        for host, pev, edict in zip((0x602000, 0x603000), (0x604000, 0x604500), case['rawEdicts'], strict=True):
            self.word(host + 8, pev)
            self.word(pev + 0x238, edict)
            self.word(host + 0x1328, 0x605000)
        self.word(0x600004, 0x601000)
        self.word(0x60101c, 0x602000)
        self.word(0x122775fc, 77)
        self.u.mem_write(0x608000, struct.pack('<2I', 0x500000, case['event'] & 0xffffffff))
        self.u.reg_write(UC_X86_REG_ESP, 0x608000)
        self.u.reg_write(UC_X86_REG_ECX, 0x600000)
        self.u.emu_start(0x10995d20 if case['kind'] == 'enter' else 0x10997ef0, 0x500000, count=20000)
        assert self.open is None
        assert self.u.reg_read(UC_X86_REG_ESP) == 0x608004 + (4 if case['kind'] == 'event' else 0)
        if case['kind'] == 'enter':
            assert self.order[:2] == ['enterBase', 'timerAdd'] and self.order[-1] == 'room'
            assert (self.float_index, self.long_index, self.owner_index) == (2, 4, 2)
            assert len(self.packets) == len(self.writer_args) == 2
            for index, (packet, args) in enumerate(zip(self.packets, self.writer_args, strict=True)):
                assert packet['fields'][0]['value'] == 'sprites/burn5.spr'
                assert (args[1], args[2], args[3], args[6], args[25]) == (case['owners'][index], index, 0xffffffff, 0x1430, 2)
                assert len(bytes.fromhex(packet['payload'])) == 85
        else:
            assert self.order[0] == 'base'
            assert (self.float_index, self.long_index, self.owner_index) == (0, 0, 0)
            assert len(self.packets) == (1 if case['event'] == 2 else 0)
            if self.packets:
                assert len(bytes.fromhex(self.packets[0]['payload'])) == 47
        return dict(inputs=case, callbacks=self.order, writerArguments=self.writer_args, packets=self.packets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('mp', 'sliding-evidence', 'evidence', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    assert not args.output.exists(), 'Use a fresh output directory'
    original = Original(args.mp, args.sliding_evidence, args.evidence)
    cases = []
    raw_edicts = (0x650100, 0x650200)
    for owners, floats, longs in itertools.product(((1, 1), (32, 256), (32767, 2)),
                                                  ((2., 5.), (3.125, 4.875)),
                                                  ((20, 12, 23, 32), (23, 32, 20, 12), (21, 17, 22, 27))):
        cases.append(original.run(dict(kind='enter', event=0, owners=owners, rawEdicts=raw_edicts, float=floats, long=longs)))
    for event in (-1, 0, 1, 2, 3, 1000, 1003):
        cases.append(original.run(dict(kind='event', event=event, owners=(1, 1), rawEdicts=raw_edicts, float=(), long=())))
    assert all(module.identity(Path(x['path'])) == x for x in original.inputs)
    args.output.mkdir(parents=True)
    enter = [row for row in cases if row['inputs']['kind'] == 'enter']
    events = [row for row in cases if row['inputs']['kind'] == 'event']
    enter_file, event_file = args.output / 'meteor_enter_cases.txt', args.output / 'meteor_event_cases.txt'
    enter_file.write_text(str(len(enter)) + '\n' + '\n'.join(' '.join(map(str, [*row['inputs']['owners'], *row['inputs']['float'],
                           *row['inputs']['long'], *[p['payload'] for p in row['packets']]])) for row in enter) + '\n', 'ascii')
    event_file.write_text(str(len(events)) + '\n' + '\n'.join(str(row['inputs']['event']) + ' ' +
                          (row['packets'][0]['payload'] if row['packets'] else '-') for row in events) + '\n', 'ascii')
    result = dict(success=True, inputsUnchanged=True, inputs=original.inputs, windows=original.meteor_windows,
                  events=len(cases), packets=sum(len(row['packets']) for row in cases), cases=cases,
                  executedOriginalInstructions=[hex(x) for x in sorted(original.executed)],
                  fixtures=[module.identity(enter_file), module.identity(event_file)],
                  boundaries=dict(engine='explicit RNG, containing-edict index, registered message77 and serialization',
                                  enteredRoom='controlled false boss_room lookup; CMeteor branch not executed',
                                  state='BaseEnter, BaseEvent and timer Add are controlled ABI fixtures',
                                  clientRendered=False, deployed=False, gameplay=False, performance=False))
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', 'utf8')
    print(json.dumps({key: result[key] for key in ('success', 'events', 'packets')}))


if __name__ == '__main__':
    main()
