#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare pure Sliding helpers against unchanged matching original x86 code.

Private original DLL/IDA files are mandatory inputs and are never published by
this script. Parser/Add/Frame, factory/ctor/init and Update execute unchanged;
engine callbacks, pool allocation, time, RNG, camera and AngleVectors are
explicit controlled boundaries. Owner lookup/stamp fixtures do not establish
original interface acquisition or production runtime integration.

--particle-oracle uses the same 19-input/15-output numeric schema as the spark
oracle. Fixed Sliding fields come from a real parser/factory-created template,
not the spark particle seed. --emitter-oracle takes one hex packet per line and
returns the documented JSON object at the controlled spawn time 10.01. Only
owner-positive/wire-alpha255 emitters enter production comparison; the other
original parser fixtures remain diagnostics of the broader original interface.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

import capstone
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

INPUT_FIELDS = ('born', 'die', 'size', 'growth', 'alpha', 'originX', 'originY',
                'originZ', 'velocityX', 'velocityY', 'velocityZ', 'now',
                'previous', 'engineGravity', 'rotation', 'rotationVelocity',
                'fps', 'frames', 'frame')
OUTPUT_FIELDS = ('alive', 'size', 'alpha', 'rotation', 'die', 'originX', 'originY',
                 'originZ', 'velocityX', 'velocityY', 'velocityZ', 'frame',
                 'born', 'growth', 'rotationVelocity')
EMITTER_FIELDS = ('owner', 'offset', 'velocity', 'born', 'die', 'size', 'growth', 'alpha', 'roll')
CLIENT_SHA = 'f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd'
SPAWN_TIME = struct.unpack('<f', struct.pack('<f', 10.01))[0]


def identity(path):
    for parent in (path, *path.parents):
        if parent.is_symlink() or getattr(parent, 'is_junction', lambda: False)():
            raise ValueError('Symlink/junction is not an oracle input: ' + str(parent))
    path = path.resolve(strict=True)
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def adjacent(value, direction):
    bits = struct.unpack('<I', struct.pack('<f', value))[0]
    return struct.unpack('<f', struct.pack('<I', bits + direction))[0]


def packet(owner=7, offset=(4, -5, 6), velocity=(7, -8, 9), alpha=255):
    """Actual Sliding grammar; only owner/coordinates/alpha vary as fixture inputs."""
    coordinates = [round(value * 8) for value in (*offset, *velocity)]
    if any(value / 8 != expected for value, expected in zip(coordinates, (*offset, *velocity))):
        raise ValueError('Fixture coordinates must be representable signed-short eighths')
    data = b'sprites/ef_bat.spr\0' + struct.pack('<h6hhffI', owner, *coordinates, 3, .1, .5, 0x30)
    # rotation, FPS, mode, alpha, RGB; spread,size,gravity,fade,growth,tails.
    data += struct.pack('<7B7f', 0, 30, 5, alpha, 255, 255, 255, 0, 50, 0, .2, .2, 0, 0)
    if len(data) != 82:
        raise AssertionError('Unexpected Sliding packet width')
    return data


def make_originals(root, client, base_evidence, reader_evidence, sliding_evidence):
    emit_path = root / 'tests/csol-teddy-sparks/emitter_oracle.py'
    particle_path = root / 'tests/csol-teddy-sparks/particle_oracle.py'
    emit = module('sliding_emitter_support', emit_path)
    particle = module('sliding_particle_support', particle_path)
    if identity(client)['sha256'] != CLIENT_SHA:
        raise ValueError('Unrecognized original client')
    inputs = [identity(Path(__file__)), identity(emit_path), identity(particle_path), identity(client)]
    inputs.append(identity(root / 'tests/csol-frozen-smoke/oracle.py'))

    class Emitter(emit.Original):
        def __init__(self):
            super().__init__(client, base_evidence, reader_evidence)
            self.inputs = list(self.files)
            decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            ranges = ((0x2a445b0, 0x2a445bd), (0x2ad2c30, 0x2ad2d36),
                      (0x2ad2450, 0x2ad25bc), (0x2ad3af0, 0x2ad3b94),
                      (0x1de3cf0, 0x1de4109))
            self.sliding_windows = []
            paths = [sliding_evidence / name for name in
                     ('client-entry-raw-ida.json', 'client-lifecycle-raw-ida.json',
                      'client-initializer-raw-ida.json', 'client-baseinit-raw-ida.json',
                      'client-motion-fade-raw-ida.json', 'client-consumer-constants-raw-ida.json')]
            for path in paths:
                self.inputs.append(identity(path))
                for row in emit.unwrap(json.loads(path.read_text('utf8'))):
                    address = int(row['addr'], 16)
                    raw = bytes(int(value, 16) for value in row['data'].split())
                    if bytes(self.u.mem_read(address, len(raw))) != raw:
                        raise ValueError('IDA bytes do not match original PE: ' + hex(address))
                    self.sliding_windows.append(dict(va=hex(address), size=len(raw),
                                                    sha256=hashlib.sha256(raw).hexdigest()))
                    for instruction in decoder.disasm(raw, address):
                        if instruction.mnemonic != 'int3' and any(lo <= instruction.address < hi for lo, hi in ranges):
                            self.allowed[instruction.address] = instruction.size
            del self.callbacks[0x2ad2c30]  # Execute the actual factory, not the spark factory fixture.
            self.callbacks.update({0x501100: 'entity', 0x501120: 'stamp',
                                   0x501140: 'enabled', 0x501160: 'local',
                                   0x1de4110: 'manager', 0x1de4d40: 'allocate',
                                   0x501180: 'angle'})
            for address, target in ((0x3ae8e80, 0x501100), (0x3c2c054, 0x680000),
                                    (0x680000, 0x680100), (0x6802a4, 0x501120),
                                    (0x3f2a2bc, 0x501140), (0x3ae8e78, 0x501160),
                                    (0x3ae8e0c, 0x501180)):
                self.word(address, target)
            self.word(0x3ae8ef8, 0)  # Explicit ordinary renderer API fixture.
            self.entity_address = 0x690000
            self.current_stamp = 77
            self.word(self.entity_address + 0x2e0, 77)
            self.u.mem_write(self.entity_address + 0xc7c, struct.pack('<6f', 100, 200, 300, 90, 180, 270))
            self.word(0x658000 + 0x8c, 15)  # Independently identified ef_bat model frame count.
            self.expected_owner = 7

        def hook(self, u, address, size, context):
            stack = u.reg_read(UC_X86_REG_ESP)
            name = self.callbacks.get(address)
            if address == 0x2ad2c30:
                words = struct.unpack('<12I', u.mem_read(stack + 4, 48))
                self.events.append(dict(operation='factory', position=self.floats(stack + 4, 3),
                                        direction=self.floats(stack + 16, 3), model=words[6],
                                        size=self.floats(stack + 32)[0], alpha=self.floats(stack + 36)[0],
                                        data=self.floats(words[11], 7)))
            if name == 'sprite_load':
                if self.text(self.integer(stack + 4)) != 'sprites/ef_bat.spr':
                    raise ValueError('Unexpected sprite request')
                self.ret(eax=2)
                return
            if name == 'strncpy':
                destination, source, count = struct.unpack('<3I', u.mem_read(stack + 4, 12))
                if count not in (31, 127, 128):
                    raise AssertionError('Unexpected original strncpy count')
                raw = bytes(u.mem_read(source, count)).split(b'\0', 1)[0][:count]
                u.mem_write(destination, raw + bytes(count - len(raw)))
                self.ret(eax=destination)
                return
            if name == 'entity':
                if self.integer(stack + 4) != self.expected_owner:
                    raise AssertionError('Unexpected owner index')
                self.ret(eax=self.entity_address)
                return
            if name == 'stamp':
                self.ret(eax=self.current_stamp)
                return
            if name in ('enabled', 'local'):
                self.ret(eax=1)
                return
            if name == 'manager':
                self.ret(eax=0x680500)
                return
            if name == 'allocate':
                if self.integer(stack + 4) != 0x1e0:
                    raise AssertionError('Unexpected original allocation size')
                destination = self.next_particle
                self.next_particle += 0x200
                self.particles.append(dict(address=destination))
                self.ret(4, eax=destination)
                return
            if name == 'angle':
                _, angles, forward, right, up = struct.unpack('<5I', u.mem_read(stack, 20))
                for destination, values in zip((forward, right, up), ((1, 0, 0), (0, -1, 0), (0, 0, 1))):
                    u.mem_write(destination, struct.pack('<3f', *values))
                self.ret()
                return
            super().hook(u, address, size, context)

        def spawn(self, raw, rng=(-1, -.5, .75, 1.25)):
            self.expected_owner = struct.unpack_from('<h', raw, raw.index(0) + 1)[0]
            self.parse(raw.hex())
            self.now = SPAWN_TIME
            self.rng_values, self.rng_index = list(rng), 0
            self.call(0x2aa6650)
            if self.rng_index != 4:
                raise AssertionError('Spread0 must still consume exactly four RandomFloat calls')
            random = [event for event in self.events if event['operation'] == 'rng']
            if [event['inputs'] for event in random] != [[-1., 1.]] * 3 + [[1., 1.5]]:
                raise AssertionError('Unexpected original RNG bounds/order')
            if [event['output'] for event in random] != list(rng):
                raise AssertionError('Original did not consume the supplied RNG sequence')
            at = self.particles[-1]['address']
            if (self.integer(at + 8), self.integer(at + 0x14), self.integer(at + 0x150)) != (0x1c, 0, 0):
                raise AssertionError('Unexpected Sliding flags')
            offset_at = emit.EMITTER + (0x40 if self.expected_owner else 0x34)
            result = dict(owner=self.expected_owner, offset=self.floats(offset_at, 3),
                          velocity=self.floats(at + 0xb8, 3), born=self.floats(at + 0x6c)[0],
                          die=self.floats(at + 0x70)[0], size=self.floats(at + 0x50)[0],
                          growth=self.floats(at + 0x54)[0], alpha=self.floats(at + 0x64)[0],
                          roll=self.floats(at + 0xa8)[0])
            fields = dict(drawFlags=self.integer(at + 8), internalFlags=self.integer(at + 0x14),
                          extraFlags=self.integer(at + 0x150), fps=self.integer(at + 0x84),
                          frames=self.integer(at + 0x88), frame=self.integer(at + 0x8c),
                          mode=self.integer(at + 0x90), fade=self.floats(at + 0x68)[0],
                          gravity=self.floats(at + 0x74)[0], mass=self.floats(at + 0xf4)[0],
                          origin=self.floats(at + 0x94, 3), initialSize=self.floats(at + 0x128)[0],
                          initialAlpha=self.floats(at + 0x138)[0])
            return result, fields, bytes(self.u.mem_read(at, 0x1e0)), list(self.events)

    class Particle(particle.Original):
        def __init__(self, template):
            super().__init__(root, client, base_evidence / 'client-verification.json')
            self.template = template
            self.stage_order = []

        def guard(self, u, address, size, context):
            stages = {0x2ad3f10: 'derivedUpdate', 0x2ad3e50: 'attachedWrapper',
                      0x1de5260: 'dispatch', 0x1de10f0: 'loopAnimation',
                      0x1de2af0: 'shrink', 0x1de3430: 'growth', 0x1de3490: 'fade',
                      0x1de5130: 'rotation', 0x1de2070: 'motion', 0x1de2210: 'collision',
                      0x2ad39f0: 'camera', 0x1de1140: 'finiteAnimation'}
            if address in stages:
                self.stage_order.append(stages[address])
            super().guard(u, address, size, context)

        def run(self, values):
            if len(values) != 19 or any(not math.isfinite(v) for v in values):
                raise ValueError('Need nineteen finite input values')
            values = [self.f32(v) for v in values]
            born, die, size, growth, alpha, ox, oy, oz, vx, vy, vz, now, previous, gravity, roll, rotate, fps, frames, frame = values
            if growth != self.f32(.2) or alpha > 250 or (fps, frames) != (30, 15) or rotate != 0:
                raise ValueError('Input outside actual Sliding fields')
            u, shared = self.u, self.shared
            u.mem_write(shared.PARTICLE, self.template)
            for offset, value in ((0x84, fps), (0x88, frames), (0x8c, frame)):
                u.mem_write(shared.PARTICLE + offset, struct.pack('<i', int(value)))
            for offset, value in ((0x50, size), (0x54, growth), (0x64, alpha), (0x6c, born),
                                  (0x70, die), (0x94, ox), (0x98, oy), (0x9c, oz),
                                  (0xa8, roll), (0xb4, rotate), (0xb8, vx), (0xbc, vy), (0xc0, vz)):
                u.mem_write(shared.PARTICLE + offset, struct.pack('<f', value))
            u.mem_write(0x3634978, struct.pack('<f', previous))
            u.mem_write(0x3c4d760, struct.pack('<f', gravity))
            u.mem_write(shared.CLOCK, struct.pack('<d', now))
            u.reg_write(UC_X86_REG_ECX, shared.PARTICLE)
            u.reg_write(UC_X86_REG_ESP, shared.STACK)
            u.mem_write(shared.STACK, struct.pack('<If', shared.STOP, now))
            self.stage_order = []
            u.emu_start(0x2ad3f10, shared.STOP, count=5000)
            if u.reg_read(UC_X86_REG_EIP) != shared.STOP or u.reg_read(UC_X86_REG_ESP) != shared.STACK + 8:
                raise AssertionError('Original did not return with expected stack')
            def read(offset):
                return struct.unpack('<f', u.mem_read(shared.PARTICLE + offset, 4))[0]
            death = read(0x70)
            result = [int(death == 0 or now < death), read(0x50), read(0x64), read(0xa8), death,
                      *(read(offset) for offset in (0x94, 0x98, 0x9c, 0xb8, 0xbc, 0xc0)),
                      struct.unpack('<i', u.mem_read(shared.PARTICLE + 0x8c, 4))[0],
                      read(0x6c), read(0x54), read(0xb4)]
            if read(0x148) != 0 or read(0x6c) != born or read(0xa0) != 27 or read(0xa4) != -49:
                raise AssertionError('Unexpected original Sliding collision/birth/camera behavior')
            if 'finiteAnimation' in self.stage_order or self.stage_order[:9] != [
                    'derivedUpdate', 'attachedWrapper', 'dispatch', 'loopAnimation',
                    'shrink', 'growth', 'fade', 'rotation', 'motion']:
                raise AssertionError('Unexpected original Sliding update order')
            return [self.f32(v) for v in result]

    return Emitter, Particle, inputs


def cases(f32):
    for born in (1., 100., 4096.):
        die = f32(born + .5)
        times = {f32(born + age) for age in (0, .0001, .016, .1, .2, .3, .49, .499, .5, .6, 1.)}
        for boundary in (f32(born + f32(1 / 30)), f32(born + f32(14 / 30)), die):
            times.update((adjacent(boundary, -1), boundary, adjacent(boundary, 1)))
        for origin, velocity in (((104., 195., 306.), (0., 0., 0.)),
                                 ((16384.25, -9000.125, 64.), (17.125, -31.75, 200.5))):
            for gravity in (0., 800., 1000.):
                for now in sorted(times):
                    for delta in (0., .016, .5):
                        yield [f32(v) for v in (born, die, 50., .2, 250., *origin, *velocity,
                                               now, f32(now - delta), gravity, 1., 0., 30., 15., 0.)]


def compare_float_list(expected, actual):
    return isinstance(actual, list) and len(expected) == len(actual) and all(
        isinstance(b, (int, float)) and math.isfinite(b) and struct.pack('<f', a) == struct.pack('<f', b)
        for a, b in zip(expected, actual))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--client-dll', type=Path)
    parser.add_argument('--base-evidence', type=Path)
    parser.add_argument('--reader-evidence', type=Path)
    parser.add_argument('--sliding-evidence', type=Path)
    parser.add_argument('--executable', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    sys.dont_write_bytecode = True
    root = args.source_root.resolve(strict=True)
    client = args.client_dll or root / 'analysis/csol-model-20261017/balrog-visual-20261104/client.dll'
    base = args.base_evidence or root / 'analysis/teddy-spawn-visuals-20261111'
    reader = args.reader_evidence or root / 'analysis/teddy-spark-oracle-20261111'
    sliding = args.sliding_evidence or root / 'analysis/teddy-sliding-client-review-20261111'
    Emitter, Particle, inputs = make_originals(root, client, base, reader, sliding)
    emitter = Emitter()
    spawn, fields, template, events = emitter.spawn(packet())
    original = Particle(template)
    inputs += emitter.inputs + original.inputs
    emitter_rows = []
    for owner in (0, 7):
        for alpha in (249, 250, 255):
            for offset, velocity in (((4, -5, 6), (7, -8, 9)), ((-4096, 4095.875, .125), (0, 0, 0))):
                source = Emitter()
                raw = packet(owner, offset, velocity, alpha)
                result, fixed, _, calls = source.spawn(raw)
                emitter_rows.append(dict(packetHex=raw.hex(), wireAlpha=alpha, original=result, fields=fixed, calls=calls))
    rows = [dict(kind='independent', input=values, original=original.run(values)) for values in cases(original.f32)]
    for born in (1., 100., 4096.):
        values = [original.f32(v) for v in (born, born + .5, 50., .2, 250., 104., 195., 306., 7., -8., 9., born, born, 800., 1., 0., 30., 15., 0.)]
        for age in (0., .016, .1, .3, .499, .5):
            values[11] = original.f32(born + age)
            result = original.run(values)
            rows.append(dict(kind='chain', input=list(values), original=result))
            if not result[0]:
                break
            values[1:5] = result[4], result[1], result[13], result[2]
            values[5:11] = result[5:11]
            values[12], values[14], values[18] = values[11], result[3], result[11]
    geometry_rows = []
    bases = (((1., 0., 0.), (0., -1., 0.), (0., 0., 1.)),
             ((0., 0., 1.), (.6, -.8, 0.), (.8, .6, 0.)))
    for origin in ((104., 195., 306.), (16384.25, -9000.125, 64.)):
        for size in (50., 50.09600067138672, 50.599998474121094, 51.80000305175781, 52.993995666503906):
            for basis in bases:
                case = dict(origin=list(origin), size=size, alpha=250., rotation=1., color=[255., 255., 255.],
                            frame=0, now=100., die=100.5, basis=[list(axis) for axis in basis])
                draw = original.draw(case)
                corners = [value for command in draw['commands'] if command['operation'] == 'Vertex3fv'
                           for value in command['values']]
                geometry_rows.append(dict(input=case, originalCorners=corners, originalDraw=draw))
    failures = []
    comparable_emitters = [row for row in emitter_rows if row['original']['owner'] > 0
                          and row['wireAlpha'] == 255]
    if args.executable:
        inputs.append(identity(args.executable))
        lines = [' '.join(format(value, '.9g') for value in row['input']) + '\n' for row in rows]
        result = subprocess.run([str(args.executable.resolve()), '--particle-oracle'], input=''.join(lines),
                                text=True, capture_output=True, timeout=60, check=True)
        actual = [json.loads(line) for line in result.stdout.splitlines()]
        if len(actual) != len(rows):
            raise AssertionError('Production particle row count differs')
        for row, values in zip(rows, actual):
            row['production'] = values
            if not compare_float_list(row['original'], values):
                failures.append(row)
        result = subprocess.run([str(args.executable.resolve()), '--emitter-oracle'],
                                input=''.join(row['packetHex'] + '\n' for row in comparable_emitters),
                                text=True, capture_output=True, timeout=60, check=True)
        actual = [json.loads(line) for line in result.stdout.splitlines()]
        if len(actual) != len(comparable_emitters):
            raise AssertionError('Production emitter row count differs')
        for row, values in zip(comparable_emitters, actual):
            row['production'] = values
            if not isinstance(values, dict) or any(key not in values for key in EMITTER_FIELDS):
                raise AssertionError('Production emitter schema differs')
            for key in EMITTER_FIELDS:
                expected, value = row['original'][key], values[key]
                if not compare_float_list(expected if isinstance(expected, list) else [expected],
                                          value if isinstance(value, list) else [value]):
                    failures.append(dict(kind='emitter', field=key, row=row))
        result = subprocess.run([str(args.executable.resolve()), '--geometry-oracle'],
                                input=''.join(' '.join(format(value, '.9g') for value in
                                                     (*row['input']['origin'], row['input']['size'],
                                                      *row['input']['basis'][1], *row['input']['basis'][2])) + '\n'
                                              for row in geometry_rows),
                                text=True, capture_output=True, timeout=60, check=True)
        actual = [json.loads(line) for line in result.stdout.splitlines()]
        if len(actual) != len(geometry_rows):
            raise AssertionError('Production geometry row count differs')
        for row, values in zip(geometry_rows, actual):
            row['productionCorners'] = values
            if not compare_float_list(row['originalCorners'], values):
                failures.append(dict(kind='geometry', row=row))
    unchanged = all(identity(Path(row['path'])) == row for row in inputs)
    report = dict(success=unchanged and not failures, inputsUnchanged=unchanged, inputs=inputs,
                  originalOnly=args.executable is None, productionCompared=args.executable is not None,
                  inputFields=INPUT_FIELDS, outputFields=OUTPUT_FIELDS, emitterFields=EMITTER_FIELDS,
                  spawnTime=SPAWN_TIME, templateSpawn=spawn, templateFields=fields, templateCalls=events,
                  callbackBoundaries=dict(owner='controlled returned entity memory and stamp77; acquisition identity not established',
                                          allocation='blank memory; actual factory/constructor/base and derived initialization',
                                          time='controlled float64', rng='three [-1,1], then mass [1,1.5], including spread0',
                                          camera=[27, -49, 0], angleVectors='controlled orthonormal basis', rendererApi=None),
                  stageOrder=original.stage_order, emitterRows=emitter_rows, rows=rows,
                  emitterCases=len(emitter_rows), particleCases=len(rows), geometryCases=len(geometry_rows),
                  comparedEmitterCases=len(comparable_emitters) if args.executable else 0,
                  comparedParticleCases=len(rows) if args.executable else 0,
                  comparedGeometryCases=len(geometry_rows) if args.executable else 0,
                  geometryRows=geometry_rows, failureCount=len(failures),
                  failures=failures, originalFrameInstructions=[hex(v) for v in sorted(emitter.executed)],
                  originalUpdateInstructions=[hex(v) for v in sorted(original.visited)],
                  slidingWindows=emitter.sliding_windows, built=False, deployed=False, liveGameplay=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf8')
    print(json.dumps({key: report[key] for key in ('success', 'inputsUnchanged', 'originalOnly', 'emitterCases', 'particleCases', 'geometryCases', 'comparedEmitterCases', 'comparedParticleCases', 'comparedGeometryCases', 'failureCount')}))
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
