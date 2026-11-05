# SPDX-License-Identifier: GPL-3.0-or-later
"""Run unchanged, IDA-verified MP instructions to generate message fixtures.

Only engine message callbacks, the entity-index lookup and caller RNG are
substituted. No DLL is loaded. Blood coverage is the isolated MPToCL packet,
not the preceding blood-policy decision or TE_BLOODSTREAM.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EIP, UC_X86_REG_ESP


MP_SHA = '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'


def identity(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


class Original:
    def __init__(self, path, evidence):
        self.sample = identity(path)
        assert self.sample['sha256'] == MP_SHA
        data = path.read_bytes()
        nt = struct.unpack_from('<I', data, 0x3c)[0]
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
        saved = json.loads(evidence.read_text('utf8'))['mp']
        assert saved['sha256'] == MP_SHA
        self.verified = []
        for name in ('blood-hit', 'think', 'debris-writer', 'shake-radius'):
            row = next(r for r in saved['ranges'] if r['name'] == name)
            raw = bytes(int(x, 16) for x in row['ida']['result'][0]['data'].split())
            assert bytes(self.u.mem_read(int(row['va'], 16), len(raw))) == raw
            assert hashlib.sha256(raw).hexdigest() == row['sha256']
            self.verified.append(dict(name=name, va=row['va'], bytes=len(raw), sha256=row['sha256']))
        self.u.mem_map(0x500000, 0x20000)
        self.u.mem_map(0x600000, 0x10000)
        self.callbacks = {0x501000: 'begin', 0x501010: 'end', 0x501020: 'byte',
                          0x501030: 'short', 0x501040: 'coord', 0x501050: 'random', 0x501060: 'entity'}
        for slot, address in ((0x12278664, 0x501000), (0x12278668, 0x501010),
                              (0x1227866c, 0x501020), (0x12278674, 0x501030),
                              (0x12278688, 0x501040), (0x1227871c, 0x501050), (0x122786d4, 0x501060)):
            self.word(slot, address)
        self.u.hook_add(UC_HOOK_CODE, self.hook)

    def word(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value))

    def hook(self, u, address, size, _):
        if address not in self.callbacks:
            return
        operation = self.callbacks[address]
        sp = u.reg_read(UC_X86_REG_ESP)
        ret, arg = struct.unpack('<II', u.mem_read(sp, 8))
        if operation == 'begin':
            assert self.open is None
            route, message, origin, recipient = struct.unpack('<4I', u.mem_read(sp + 4, 16))
            self.open = dict(route=route, message=message, recipient=recipient,
                             pvs=list(struct.unpack('<3f', u.mem_read(origin, 12))) if origin else None,
                             payload=bytearray())
        elif operation == 'end':
            assert self.open is not None
            self.open['payload'] = self.open['payload'].hex()
            self.packets.append(self.open)
            self.open = None
        elif operation == 'byte':
            self.open['payload'].append(arg & 255)
        elif operation == 'short':
            self.open['payload'] += struct.pack('<H', arg & 65535)
        elif operation == 'coord':
            value = struct.unpack('<f', struct.pack('<I', arg))[0]
            self.open['payload'] += struct.pack('<H', int(value * 8) & 65535)
        elif operation == 'random':
            assert struct.unpack('<2I', u.mem_read(sp + 4, 8)) == (3, 4)
            u.reg_write(UC_X86_REG_EAX, self.count)
            self.random_calls += 1
        elif operation == 'entity':
            assert arg == 1
            u.reg_write(UC_X86_REG_EAX, 0x508000)
        u.reg_write(UC_X86_REG_ESP, sp + 4)
        u.reg_write(UC_X86_REG_EIP, ret)

    def run(self, kind, id, position, model, count, grounded=True, distance=0):
        self.open, self.packets, self.random_calls = None, [], 0
        self.count = count
        self.word(0x122774d0, id)
        self.word(0x1227737c, id)
        self.u.mem_write(0x122bbdb4, struct.pack('<h', model))
        self.u.mem_write(0x505008, struct.pack('<3f', *position))
        self.word(0x505238, 0x508000)
        self.word(0x504000, 0x505000)
        self.word(0x122788f4, 0x507000)
        self.word(0x507090, 1)
        self.word(0x508000, 0)
        self.word(0x508080, 0x509000)
        self.word(0x509008, 0x50a000)
        self.word(0x50a238, 0x508000)
        self.word(0x50a1c8, 0x200 if grounded else 0)
        player_position = (position[0] + distance, *position[1:])
        self.u.mem_write(0x50a008, struct.pack('<3f', *player_position))
        self.u.reg_write(UC_X86_REG_ESP, 0x608000)
        if kind == 0:
            self.u.reg_write(UC_X86_REG_EAX, 0x505000)
            self.u.emu_start(0x113e7bd2, 0x113e7bfb, count=100)
        elif kind == 1:
            self.u.mem_write(0x608000, struct.pack('<II5f', 0x500000, 0x505008, 4, 3, 1, 300, 0))
            self.u.emu_start(0x11532450, 0x500000, count=1000)
        else:
            self.u.reg_write(UC_X86_REG_EBX, 0x504000)
            self.u.emu_start(0x113e9f8a, 0x113e9ff1, count=1000)
        assert self.open is None
        assert self.random_calls == (1 if kind == 2 else 0)
        return self.packets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mp', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert not args.output.exists(), 'Do not overwrite earlier evidence'
    args.output.mkdir(parents=True)
    original = Original(args.mp, args.evidence)
    lines, rows = [], []
    for kind in range(3):
        for id in (64, 96, 244):
            for position in ((0., 0., 0.), (-12.25, 100.5, 43.125), (-4096., 4095.875, -.125)):
                for model in ((1, 32767) if kind == 2 else (1,)):
                    for count in ((3, 4) if kind == 2 else (3,)):
                        packets = original.run(kind, id, position, model, count)
                        assert len(packets) == 1
                        packet = packets[0]
                        assert packet['message'] == (23 if kind == 2 else id)
                        assert packet['route'] == (4 if kind == 2 else 1)
                        assert packet['pvs'] == (list(position) if kind == 2 else None)
                        assert packet['recipient'] == (0 if kind == 2 else 0x508000)
                        lines.append(' '.join(map(str, (kind, id, *position, model, count, packet['route'], packet['payload']))))
                        rows.append(dict(kind=kind, id=id, position=position, model=model, count=count, packets=packets))
    excluded = []
    for grounded, distance in ((False, 0), (True, 300), (True, 301)):
        packets = original.run(1, 96, (0, 0, 0), 1, 3, grounded, distance)
        assert not packets
        excluded.append(dict(grounded=grounded, distance=distance, packets=packets))
    fixture = args.output / 'cases.txt'
    fixture.write_text(str(len(rows)) + '\n' + '\n'.join(lines) + '\n', 'ascii')
    (args.output / 'result.json').write_text(json.dumps(dict(sample=original.sample, evidence=identity(args.evidence),
        verified=original.verified, fixture=identity(fixture), cases=rows, excluded=excluded, success=True), indent=2), 'utf8')
    print(len(rows), 'original packet cases;', len(excluded), 'original recipient exclusions')


if __name__ == '__main__':
    main()
