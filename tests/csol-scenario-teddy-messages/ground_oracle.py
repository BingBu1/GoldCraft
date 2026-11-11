# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture Teddy ground packets from unchanged, saved-IDA-verified MP code.

This is a serializer/order oracle, not a geometry oracle: VecToAngles and
Forward are explicit fixture outputs. CRT rand and engine RandomFloat are
explicit fixture inputs. Writable MP velocity global 12278584 is seeded,
never inferred to be zero. No native DLL or game executable is loaded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

MP_SHA = '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'


def identity(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def unwrap(value):
    if isinstance(value, dict) and 'content' in value:
        return unwrap(json.loads(next(r['text'] for r in value['content'] if r['type'] == 'text')))
    return value['result'] if isinstance(value, dict) and 'result' in value else value


class GroundOriginal:
    def __init__(self, sample, evidence, dust):
        self.sample = identity(sample)
        assert self.sample['sha256'] == MP_SHA
        data = sample.read_bytes()
        nt = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[nt:nt + 4] == b'PE\0\0'
        assert struct.unpack_from('<H', data, nt + 4)[0] == 0x14c
        optional = nt + 24
        base = struct.unpack_from('<I', data, optional + 28)[0]
        size = struct.unpack_from('<I', data, optional + 56)[0]
        assert base == 0x10000000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(base, (size + 4095) & ~4095)
        headers = optional + struct.unpack_from('<H', data, nt + 20)[0]
        for i in range(struct.unpack_from('<H', data, nt + 6)[0]):
            _, rva, length, offset = struct.unpack_from('<4I', data, headers + i * 40 + 8)
            self.u.mem_write(base + rva, data[offset:offset + length])
        self.verified, self.files = [], []
        for path in (evidence / 'ground-bytes.json', evidence / 'helpers-bytes.json',
                     evidence / 'model-writer-bytes.json', dust):
            self.files.append(identity(path))
            for row in unwrap(json.loads(path.read_text('utf8'))):
                address = int(row['addr'], 16)
                raw = bytes(int(b, 16) for b in row['data'].split())
                assert bytes(self.u.mem_read(address, len(raw))) == raw, hex(address)
                self.verified.append(dict(va=address, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                                          evidence=str(path)))
        self.u.mem_map(0x500000, 0x10000)
        self.u.mem_map(0x600000, 0x10000)
        self.callbacks = {0x501000: 'begin', 0x501010: 'end', 0x501020: 'byte',
                          0x501030: 'short', 0x501040: 'coord', 0x501050: 'angle',
                          0x501060: 'long', 0x501070: 'rand', 0x501080: 'randomfloat',
                          0x11533950: 'vectorangles', 0x11530060: 'forward'}
        for slot, address in ((0x12278664, 0x501000), (0x12278668, 0x501010),
                              (0x1227866c, 0x501020), (0x12278674, 0x501030),
                              (0x12278688, 0x501040), (0x12278684, 0x501050),
                              (0x12278678, 0x501060), (0x117404d4, 0x501070),
                              (0x12278720, 0x501080)):
            self.word(slot, address)
        # Explicit engine RandomFloat ABI stub: x87 float return, caller cleanup.
        self.u.mem_write(0x501080, b'\xd9\x05' + struct.pack('<I', 0x502000) + b'\xc3')
        self.u.hook_add(UC_HOOK_CODE, self.hook)
        self.executed = set()

    def word(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value))

    def vec(self, address):
        return list(struct.unpack('<3f', self.u.mem_read(address, 12)))

    def hook(self, u, address, size, _):
        if address not in self.callbacks:
            if 0x501080 < address < 0x501087:
                return
            assert any(r['va'] <= address and address + size <= r['va'] + r['bytes']
                       for r in self.verified), 'Unverified original instruction ' + hex(address)
            self.executed.add(address)
            return
        op = self.callbacks[address]
        sp = u.reg_read(UC_X86_REG_ESP)
        ret, arg = struct.unpack('<II', u.mem_read(sp, 8))
        if op == 'begin':
            assert self.open is None
            route, message, origin, recipient = struct.unpack('<4I', u.mem_read(sp + 4, 16))
            self.open = dict(route=route, message=message, recipient=recipient,
                             pvs=self.vec(origin) if origin else None, events=[], payload=bytearray())
        elif op == 'end':
            assert self.open is not None
            self.open['payload'] = self.open['payload'].hex()
            self.packets.append(self.open)
            self.open = None
        elif op in ('byte', 'short', 'long', 'coord', 'angle'):
            value = struct.unpack('<f', struct.pack('<I', arg))[0] if op in ('coord', 'angle') else arg
            self.open['events'].append(dict(operation=op, value=value, raw=arg))
            if op == 'byte':
                self.open['payload'].append(arg & 255)
            elif op == 'short':
                self.open['payload'] += struct.pack('<H', arg & 65535)
            elif op == 'long':
                self.open['payload'] += struct.pack('<I', arg)
            elif op == 'coord':
                self.open['payload'] += struct.pack('<H', int(value * 8) & 65535)
            else:
                self.open['payload'].append(int(value * 256 / 360) & 255)
        elif op == 'rand':
            u.reg_write(UC_X86_REG_EAX, self.case['crtRand'])
            self.stub_calls.append(dict(operation=op, output=self.case['crtRand']))
        elif op == 'randomfloat':
            low, high = struct.unpack('<2f', u.mem_read(sp + 4, 8))
            value = self.case['randomFloat'][self.random_index]
            self.random_index += 1
            assert low <= value <= high, (low, high, value)
            u.mem_write(0x502000, struct.pack('<f', value))
            self.stub_calls.append(dict(operation=op, inputs=[low, high], output=value))
            return  # Execute the documented x87 fixture stub.
        elif op in ('vectorangles', 'forward'):
            a, b = struct.unpack('<2I', u.mem_read(sp + 4, 8))
            output, source = (a, b) if op == 'vectorangles' else (b, a)
            value = self.case['vectorAngles' if op == 'vectorangles' else 'forward']
            self.stub_calls.append(dict(operation=op, input=self.vec(source), output=list(value)))
            u.mem_write(output, struct.pack('<3f', *value))
            u.reg_write(UC_X86_REG_EAX, output)
        u.reg_write(UC_X86_REG_ESP, sp + 4)
        u.reg_write(UC_X86_REG_EIP, ret)

    def run(self, case):
        self.case, self.open, self.packets = case, None, []
        self.stub_calls, self.random_index = [], 0
        for address, key in ((0x122bbdb0, 'dustModel'), (0x11ef7b4c, 'trailModel'),
                             (0x11ef7b48, 'effectModel'), (0x11ef7b50, 'explosionModel')):
            self.u.mem_write(address, struct.pack('<H', case[key]))
        self.u.mem_write(0x12278584, struct.pack('<3f', *case['velocityGlobal']))
        self.u.mem_write(0x608000, struct.pack('<I8f', 0x500000, *case['start'], *case['target'],
                                            case['distance'], case['yawOffset']))
        self.u.reg_write(UC_X86_REG_ESP, 0x608000)
        self.u.emu_start(0x10996d40, 0x500000, count=20000)
        assert self.open is None and self.random_index == 2 and len(self.packets) == 4
        assert [bytes.fromhex(p['payload'])[0] for p in self.packets] == [108, 133, 145, 3]
        return dict(inputs=case, stubCalls=self.stub_calls, packets=self.packets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mp', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--dust-evidence', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert not args.output.exists(), 'Do not overwrite earlier evidence'
    original = GroundOriginal(args.mp, args.evidence, args.dust_evidence)
    cases = []
    for start in ((0., 0., 32.), (-12.25, 100.5, 43.125), (2048.25, -2000.5, 100.125)):
        for models in ((1, 2, 3, 4), (32767, 30000, 25000, 20000)):
            for variant in range(3):
                case = dict(start=start, target=[start[0] + 20, start[1] - 40, start[2] + 10],
                            vectorAngles=(10., 25., -5.), yawOffset=(-60., 0., 45.)[variant],
                            forward=((1., 0., 0.), (0., -1., 0.), (.6, .8, 0.))[variant], distance=20.,
                            crtRand=(0, 16384, 32767)[variant],
                            randomFloat=((-16., 16.), (-.125, .25), (12.5, -7.75))[variant],
                            velocityGlobal=((7.25, -8.5, 9.125), (-2., 3.5, -4.25), (0., 0., 0.))[variant],
                            dustModel=models[0], trailModel=models[1], effectModel=models[2], explosionModel=models[3])
                cases.append(original.run(case))
    lines = []
    for case in cases:
        inp = case['inputs']
        angles = next(c['input'] for c in case['stubCalls'] if c['operation'] == 'forward')
        for kind, packet in enumerate(case['packets']):
            coords = [e['value'] for e in packet['events'] if e['operation'] == 'coord']
            yaw = next((e['value'] for e in packet['events'] if e['operation'] == 'angle'), 0)
            model = inp[('dustModel', 'trailModel', 'effectModel', 'explosionModel')[kind]]
            lines.append(' '.join(map(str, (kind, *coords[:3], *angles, *inp['velocityGlobal'], model, yaw,
                                            packet['route'], int(packet['pvs'] is not None),
                                            *(packet['pvs'] or (0, 0, 0)), packet['payload']))))
    args.output.mkdir(parents=True)
    fixture = args.output / 'ground_cases.txt'
    fixture.write_text(str(len(lines)) + '\n' + '\n'.join(lines) + '\n', 'ascii')
    result = dict(success=True, scope='original serializer/order with explicit math and RNG fixtures',
                  sample=original.sample, evidence=original.files, verifiedRanges=original.verified,
                  executedOriginalInstructions=[hex(a) for a in sorted(original.executed)],
                  fixture=identity(fixture), cases=cases,
                  boundaries=dict(original=['10996D40', '113E7810', '1148E500', '115302F0', '115303C0'],
                                  substituted=['11533950 VecToAngles', '11530060 Forward', 'CRT rand IAT117404D4',
                                               'engine RandomFloat slot12278720', 'engine message callbacks'],
                                  wire='GoldSrc WriteCoord trunc(value*8), WriteAngle trunc(value*256/360); raw callback events retained',
                                  unknown='Runtime meaning/value of writable12278584; full caller geometry and original engine callbacks not executed'))
    (args.output / 'result.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(len(cases), 'original ground calls;', len(lines), 'ordered original packets')


if __name__ == '__main__':
    main()
