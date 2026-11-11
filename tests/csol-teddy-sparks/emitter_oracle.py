#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay unchanged original x86 Teddy TempEmit parser/Add/Frame instructions.

Private DLL/IDA captures are mandatory inputs, never published by this test.
Engine time, sprite lookup, particle factory, RNG and CRT are explicit fixtures.
Factory inputs and post-factory Frame writes are recorded separately; this is
not a renderer, factory-constructor, native engine RNG or real gameplay test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

CLIENT_SHA = 'f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
MANAGER, MESSAGE, STACK, STOP = 0x600000, 0x650000, 0x67f000, 0x500000
EMITTER, STRIDE, COUNT = MANAGER + 0xb414, 0x168, 128


def identity(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def unwrap(value):
    if isinstance(value, dict):
        if 'structuredContent' in value:
            return unwrap(value['structuredContent'])
        if 'jsonrpc' in value:
            return unwrap(value['result'])
        if 'content' in value:
            return unwrap(json.loads(next(row['text'] for row in value['content'] if row['type'] == 'text')))
        if 'result' in value:
            return unwrap(value['result'])
    return value


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class Original:
    def __init__(self, sample, evidence, extra):
        self.sample = identity(sample)
        assert self.sample['sha256'] == CLIENT_SHA
        data = sample.read_bytes()
        nt = struct.unpack_from('<I', data, 0x3c)[0]
        assert data[nt:nt + 4] == b'PE\0\0' and struct.unpack_from('<H', data, nt + 4)[0] == 0x14c
        opt = nt + 24
        base, size = struct.unpack_from('<I', data, opt + 28)[0], struct.unpack_from('<I', data, opt + 56)[0]
        assert base == 0x01900000
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(base, (size + 4095) & ~4095)
        sections = opt + struct.unpack_from('<H', data, nt + 20)[0]
        for i in range(struct.unpack_from('<H', data, nt + 6)[0]):
            _, rva, length, offset = struct.unpack_from('<4I', data, sections + 40 * i + 8)
            self.u.mem_write(base + rva, data[offset:offset + length])
        self.files, self.verified, self.allowed, self.executed = [], [], {}, set()
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        paths = [evidence / 'client-preliminary-bytes.json', evidence / 'client-particle-raw-ida.json',
                 extra / 'reader-bytes-ida.json', extra / 'constants-bytes-ida.json']
        for path in paths:
            self.files.append(identity(path))
            for row in unwrap(json.loads(path.read_text('utf8'))):
                address = int(row['addr'], 16)
                raw = bytes(int(b, 16) for b in row['data'].split())
                assert bytes(self.u.mem_read(address, len(raw))) == raw, hex(address)
                self.verified.append(dict(va=hex(address), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                                          evidence=str(path)))
                # Exact instruction starts and sizes, only in relevant routines.
                for ins in decoder.disasm(raw, address):
                    ranges = ((0x2aa4240, 0x2aa4280), (0x2aa4360, 0x2aa4680),
                              (0x2aa49b0, 0x2aa4a82), (0x2aa54a0, 0x2aa570f),
                              (0x2aa6650, 0x2aa7016), (0x2519cf0, 0x2519d84))
                    if ins.mnemonic != 'int3' and any(lo <= ins.address < hi for lo, hi in ranges):
                        self.allowed[ins.address] = ins.size
        self.u.mem_map(0x500000, 0x10000)
        self.u.mem_map(0x600000, 0x100000)
        self.callbacks = {0x501000: 'time', 0x501020: 'sprite_load', 0x501040: 'sprite_model',
                          0x501060: 'print', 0x501080: 'rng', 0x5010a0: 'strncpy',
                          0x2da8819: 'memset', 0x2d5b8d5: 'security_cookie', 0x2ad2c30: 'factory'}
        for slot, target in ((0x3ae8e84, 0x501000), (0x3ae8da8, 0x501020),
                             (0x3ae8ea4, 0x501040), (0x3ae8e50, 0x501060),
                             (0x3ae8eb8, 0x501080), (0x2f78798, 0x5010a0)):
            self.word(slot, target)
        # x87 return ABI for the time and RandomFloat fixture boundaries.
        for target in (0x501000, 0x501080):
            self.u.mem_write(target, b'\xd9\x05' + struct.pack('<I', 0x502000) + b'\xc3')
        self.u.hook_add(UC_HOOK_CODE, self.hook)
        self.reset()

    def word(self, at, value):
        self.u.mem_write(at, struct.pack('<I', value & 0xffffffff))

    def integer(self, at):
        return struct.unpack('<i', self.u.mem_read(at, 4))[0]

    def floats(self, at, count=1):
        return list(struct.unpack('<' + 'f' * count, self.u.mem_read(at, 4 * count)))

    def text(self, at, count=128):
        return bytes(self.u.mem_read(at, count)).split(b'\0', 1)[0].decode('ascii')

    def reset(self):
        self.u.mem_write(MANAGER, bytes(0x20000))
        self.u.mem_write(0x660000, bytes(0x10000))
        self.now, self.rng_values, self.rng_index, self.fail_factory = 10., [], 0, None
        self.events, self.particles, self.logs, self.loads = [], [], [], []
        self.next_particle, self.factory_count = 0x660000, 0
        # Native fixed-point coordinate mode <= 1 uses signed short * .125.
        self.u.mem_write(0x35cc1c8, struct.pack('<f', 1.))
        self.u.mem_write(0x3ae91d4, struct.pack('<f', self.now))
        for model in (0x658000, 0x658100):
            self.word(model + 0x8c, 8)

    def ret(self, cleanup=0, eax=None):
        sp = self.u.reg_read(UC_X86_REG_ESP)
        dest = self.integer(sp) & 0xffffffff
        if eax is not None:
            self.u.reg_write(UC_X86_REG_EAX, eax & 0xffffffff)
        self.u.reg_write(UC_X86_REG_ESP, sp + 4 + cleanup)
        self.u.reg_write(UC_X86_REG_EIP, dest)

    def hook(self, u, address, size, _):
        if address in (0x501006, 0x501086):
            return
        name = self.callbacks.get(address)
        if name is None:
            assert self.allowed.get(address) == size, 'Unsupported original instruction ' + hex(address)
            self.executed.add(address)
            return
        sp = u.reg_read(UC_X86_REG_ESP)
        if name == 'time':
            u.mem_write(0x502000, struct.pack('<f', self.now))
            return
        if name == 'rng':
            low, high = self.floats(sp + 4, 2)
            assert self.rng_index < len(self.rng_values), 'Unexpected RNG call'
            value = self.rng_values[self.rng_index]
            self.rng_index += 1
            assert low <= value <= high
            self.events.append(dict(operation='rng', inputs=[low, high], output=value))
            u.mem_write(0x502000, struct.pack('<f', value))
            return
        if name == 'memset':
            dest, value, count = struct.unpack('<3I', u.mem_read(sp + 4, 12))
            assert count == 0x164
            u.mem_write(dest, bytes([value & 255]) * count)
            self.ret(eax=dest)
        elif name == 'strncpy':
            dest, source, count = struct.unpack('<3I', u.mem_read(sp + 4, 12))
            assert count == 128
            raw = bytes(u.mem_read(source, count)).split(b'\0', 1)[0][:count]
            u.mem_write(dest, raw + bytes(count - len(raw)))
            self.ret(eax=dest)
        elif name == 'security_cookie':
            self.ret()
        elif name == 'sprite_load':
            path = self.text(self.integer(sp + 4))
            assert path in ('sprites/spark2.spr', 'sprites/spark3.spr')
            self.loads.append(path)
            self.ret(eax=2 if path.endswith('2.spr') else 3)
        elif name == 'sprite_model':
            handle = self.integer(sp + 4)
            assert handle in (2, 3)
            self.ret(eax=0x658000 + (handle - 2) * 0x100)
        elif name == 'print':
            self.logs.append(self.text(self.integer(sp + 4)))
            self.ret()
        elif name == 'factory':
            words = struct.unpack('<12I', u.mem_read(sp + 4, 48))
            self.factory_count += 1
            row = dict(operation='factory', attempt=self.factory_count,
                       position=self.floats(sp + 4, 3), direction=self.floats(sp + 16, 3),
                       model=words[6], size=self.floats(sp + 32)[0], alpha=self.floats(sp + 36)[0],
                       name=self.text(words[9]), type=words[10], data=self.floats(words[11], 7))
            row['dataRaw'] = bytes(u.mem_read(words[11], 0x80)).hex()
            self.events.append(row)
            if self.fail_factory == self.factory_count:
                row['failed'] = True
                self.ret(eax=0)
                return
            dest = self.next_particle
            self.next_particle += 0x200
            u.mem_write(dest, bytes(0x200))  # Explicit blank factory fixture.
            self.particles.append(dict(address=dest, factory=row))
            self.ret(eax=dest)

    def call(self, address, args=()):
        self.u.mem_write(STACK, struct.pack('<' + 'I' * (1 + len(args)), STOP, *args))
        self.u.reg_write(UC_X86_REG_ESP, STACK)
        self.u.reg_write(UC_X86_REG_ECX, MANAGER)
        self.u.emu_start(address, STOP, count=200000)
        assert self.u.reg_read(UC_X86_REG_EIP) == STOP, 'Original did not return'
        assert self.u.reg_read(UC_X86_REG_ESP) == STACK + 4 * (len(args) + 1)

    def parse(self, payload):
        raw = bytes.fromhex(payload)
        assert len(raw) == 82
        self.u.mem_write(MESSAGE, raw)
        self.call(0x2aa54a0, (0, len(raw), MESSAGE))
        assert self.integer(0x3ede154) == 82 and self.integer(0x3ede158) == 0
        assert self.u.reg_read(UC_X86_REG_EAX) == 1

    def emitter(self, index):
        at = EMITTER + index * STRIDE
        return dict(slot=index, owner=self.integer(at + 4), total=self.integer(at + 0x10),
                    remaining=self.integer(at + 0x1c), interval=self.floats(at + 0x24)[0],
                    next=self.floats(at + 0x28)[0], life=self.floats(at + 0x20)[0],
                    flags=self.integer(at + 0x2c), position=self.floats(at + 0x34, 3),
                    velocity=self.floats(at + 0x4c, 3), spread=self.floats(at + 0x30)[0],
                    color=self.floats(at + 0x58, 3), sprite=self.text(at + 0x74),
                    rotation=self.integer(at + 0x130), alpha=self.floats(at + 0x134)[0],
                    fps=self.integer(at + 0x13c), mode=self.integer(at + 0x144))

    def frame(self, now, rng, fail=None):
        self.now, self.rng_values, self.rng_index, self.fail_factory = f32(now), list(rng), 0, fail
        start_event, start_particle = len(self.events), len(self.particles)
        self.call(0x2aa6650)
        assert self.rng_index == len(rng), (self.rng_index, len(rng))
        particles = []
        for particle in self.particles[start_particle:]:
            p = particle['address']
            particles.append(dict(factory=particle['factory'],
                velocity=self.floats(p + 0xb8, 3), die=self.floats(p + 0x70)[0],
                frame=self.integer(p + 0x8c), fps=self.integer(p + 0x84), frames=self.integer(p + 0x88),
                internalFlags=self.integer(p + 0x14), flags=self.integer(p + 8),
                mass=self.floats(p + 0xf4)[0], rotation=self.floats(p + 0xac, 3),
                field7c=self.floats(p + 0x7c)[0], field80=self.floats(p + 0x80)[0],
                growth=self.floats(p + 0x54)[0], color=self.floats(p + 0xe8, 3),
                mode=self.integer(p + 0x90)))
        return dict(now=self.now, events=self.events[start_event:], particles=particles,
                    emitters=[self.emitter(0), self.emitter(1)])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('client', 'evidence', 'extra-evidence', 'packets', 'output'):
        ap.add_argument('--' + name, required=True, type=Path)
    args = ap.parse_args()
    assert not args.output.exists(), 'Use a fresh output directory'
    files = [identity(Path(__file__)), identity(args.packets)]
    original = Original(args.client, args.evidence, args.extra_evidence)
    capture = json.loads(args.packets.read_text('utf8'))
    assert capture['success'] and capture['sample']['sha256'] == '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'
    packet_cases = [case for case in capture['cases'] if case['event'] == 0]
    cases = []
    rng = (-1., .25, 1., 1.25)
    for index, source in enumerate(packet_cases):
        original.reset()
        for packet in source['packets']:
            original.parse(packet['payload'])
        parsed = [original.emitter(0), original.emitter(1)]
        assert [row['sprite'] for row in parsed] == ['sprites/spark2.spr', 'sprites/spark3.spr']
        for slot, row in enumerate(parsed):
            assert row['total'] == row['remaining'] == 3 and row['flags'] == 0x50
            assert row['life'] == f32(1.8) and row['mode'] == 5 and row['fps'] == 16 + slot
            assert row['spread'] == 30 and row['velocity'] == [0., 0., 200.]
        equal = original.frame(10., ())
        assert not equal['particles']
        frames = []
        for now in (10.5, 11.5, 12.5):
            frame = original.frame(now, rng * 2)
            assert len(frame['particles']) == 2
            for slot, particle in enumerate(frame['particles']):
                assert particle['velocity'] == [-30., 7.5, 230.]
                assert particle['internalFlags'] == 0x4000 and particle['fps'] == 16 + slot
                assert particle['mode'] == 5 and particle['color'] == parsed[slot]['color']
                assert particle['mass'] == 1.25 and particle['rotation'][:2] == [0., 0.]
                assert particle['rotation'][2] == f32(parsed[slot]['rotation'] * f32(.1))
                assert particle['die'] == f32(f32(now) + f32(1.8))
            frames.append(frame)
        exhausted = original.frame(100., ())
        assert not exhausted['particles'] and all(row['remaining'] == 0 for row in exhausted['emitters'])
        cases.append(dict(name='packet-' + str(index), source=source, parsed=parsed,
                          equal=equal, frames=frames, exhausted=exhausted))
    # Strict scheduling, no catch-up, including equality after one emission.
    original.reset()
    for packet in packet_cases[0]['packets']:
        original.parse(packet['payload'])
    first = original.frame(100., rng * 2)
    assert len(first['particles']) == 2 and [r['remaining'] for r in first['emitters']] == [2, 2]
    at_next = first['emitters'][0]['next']
    strict = original.frame(at_next, ())
    assert not strict['particles']
    cases.append(dict(name='strict-next-no-catchup', first=first, equalNext=strict))
    # Original treats a zero next-time as disabled even after now advances.
    original.reset()
    original.u.mem_write(0x3ae91d4, struct.pack('<f', 0.))
    for packet in packet_cases[0]['packets']:
        original.parse(packet['payload'])
    zero_next = original.frame(1., ())
    assert not zero_next['particles']
    assert all(row['remaining'] == 3 and row['next'] == 0. for row in zero_next['emitters'])
    cases.append(dict(name='zero-next-disabled', frame=zero_next))
    # Factory exhaustion consumes emitter0's count/next; emitter1 remains untouched.
    original.reset()
    for packet in packet_cases[0]['packets']:
        original.parse(packet['payload'])
    failed = original.frame(10.5, (), fail=1)
    assert not failed['particles'] and [r['remaining'] for r in failed['emitters']] == [2, 3]
    assert failed['emitters'][0]['next'] > 10.5 and failed['emitters'][1]['next'] == 10.
    recovered = original.frame(10.5, rng)
    assert len(recovered['particles']) == 1 and recovered['particles'][0]['fps'] == 17
    cases.append(dict(name='factory-full-stops-frame', failed=failed, recovered=recovered))
    # Add uses remaining==0 as first-free predicate and copies into that slot only.
    original.reset()
    for i in range(COUNT):
        original.word(EMITTER + i * STRIDE + 0x1c, 1)
    original.word(EMITTER + 7 * STRIDE + 0x1c, 0)
    original.word(EMITTER + 41 * STRIDE + 0x1c, 0)
    original.parse(packet_cases[0]['packets'][0]['payload'])
    assert original.emitter(7)['remaining'] == 3 and original.emitter(41)['remaining'] == 0
    assert original.loads == ['sprites/spark2.spr']
    original.word(EMITTER + 41 * STRIDE + 0x1c, 1)
    before = bytes(original.u.mem_read(EMITTER, COUNT * STRIDE))
    original.parse(packet_cases[0]['packets'][1]['payload'])
    assert bytes(original.u.mem_read(EMITTER, COUNT * STRIDE)) == before
    assert len(original.logs) == 1 and original.loads == ['sprites/spark2.spr']
    cases.append(dict(name='128-pool-first-free-and-full', firstFree=original.emitter(7),
                      unused=original.emitter(41), log=original.logs))
    all_inputs = files + original.files + [original.sample]
    assert all(identity(Path(row['path'])) == row for row in all_inputs)
    args.output.mkdir(parents=True)
    result = dict(success=True, inputsUnchanged=True, sample=original.sample, inputs=all_inputs,
                  originalByteWindows=original.verified, cases=cases,
                  executedOriginalInstructions=[hex(address) for address in sorted(original.executed)],
                  boundaries=dict(unchangedOriginal='parser/reader/constructor/Add/Frame',
                      decodedInstructions='Capstone boundaries of IDA bytes verified against pinned original PE',
                      factory='explicit blank returned particle; arguments and data recorded, constructor not executed; no gravity/fade-constructor claim',
                      sprite='explicit handles2/3, model frame count8; no filesystem loader',
                      RNG='explicit engine RandomFloat ABI inputs; actual algorithm not executed',
                      time='explicit client time and Add time global10',
                      crt='memset,strncpy,security-cookie fixture',
                      rendererExecuted=False, deployed=False, realGameplay=False))
    (args.output / 'result.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(json.dumps(dict(success=True, cases=len(cases), originalInstructionStarts=len(original.executed))))


if __name__ == '__main__':
    main()
