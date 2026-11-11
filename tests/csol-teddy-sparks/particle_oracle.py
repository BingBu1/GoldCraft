#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute original Teddy spark Update instructions and compare production Tick.

Uses the existing frozen-smoke oracle's sample identity, ABI addresses and
function allowlist. The finite-frame vtable branch is added from saved IDA
evidence. No original DLL is loaded natively and no production tree is copied.
Engine time and camera are explicit controlled fixture boundaries.

--executable is optional for recording original-only evidence. Production
--particle-oracle reads 19 numeric fields per line in INPUT_FIELDS order and
returns one JSON array with exactly 15 OUTPUT_FIELDS values per line.
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

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

INPUT_FIELDS = ('born', 'die', 'size', 'growth', 'alpha', 'originX', 'originY',
                'originZ', 'velocityX', 'velocityY', 'velocityZ', 'now',
                'previous', 'engineGravity', 'rotation', 'rotationVelocity',
                'fps', 'frames', 'frame')
OUTPUT_FIELDS = ('alive', 'size', 'alpha', 'rotation', 'die', 'originX', 'originY',
                 'originZ', 'velocityX', 'velocityY', 'velocityZ', 'frame',
                 'born', 'growth', 'rotationVelocity')


def identity(path):
    for parent in (path, *path.parents):
        if parent.is_symlink() or getattr(parent, 'is_junction', lambda: False)():
            raise ValueError('Symlink/junction is not an oracle input: ' + str(parent))
    path = path.resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.file_digest(path.open('rb'), 'sha256').hexdigest())


def adjacent(value, direction):
    """Adjacent positive finite float32; used for strict time branch boundaries."""
    bits = struct.unpack('<I', struct.pack('<f', value))[0]
    return struct.unpack('<f', struct.pack('<I', bits + direction))[0]


class Original:
    def __init__(self, root, client, verification):
        sys.dont_write_bytecode = True
        helper = root / 'tests/csol-frozen-smoke/oracle.py'
        spec = importlib.util.spec_from_file_location('frozen_particle_original', helper)
        shared = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(shared)
        self.shared, self.f32 = shared, shared.f32
        self.inputs = [identity(Path(__file__)), identity(helper), identity(client), identity(verification)]
        evidence = json.loads(verification.read_text('utf-8-sig'))
        if not evidence.get('success') or evidence.get('sha256') != shared.CLIENT_SHA:
            raise ValueError('Need matching successful original-client verification')
        if self.inputs[2]['sha256'] != shared.CLIENT_SHA:
            raise ValueError('Unrecognized original client sample')
        raw = client.read_bytes()
        pe = struct.unpack_from('<I', raw, 0x3c)[0]
        if raw[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', raw, pe + 4)[0] != 0x14c:
            raise ValueError('Expected original x86 PE')
        count, optional_size = struct.unpack_from('<H', raw, pe + 6)[0], struct.unpack_from('<H', raw, pe + 20)[0]
        base, image_size = struct.unpack_from('<I', raw, pe + 52)[0], struct.unpack_from('<I', raw, pe + 80)[0]
        if base != 0x01900000:
            raise ValueError('Unexpected original client image base')
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(base, (image_size + 4095) & ~4095)
        for index in range(count):
            at = pe + 24 + optional_size + index * 40
            _, rva, size, offset = struct.unpack_from('<IIII', raw, at + 8)
            self.u.mem_write(base + rva, raw[offset:offset + size])
        self.windows = []
        for window in evidence['windows']:
            address, size = int(window['va'], 16), window['size']
            actual = bytes(self.u.mem_read(address, size))
            if hashlib.sha256(actual).hexdigest() != window['sha256']:
                raise ValueError('Original evidence window changed: ' + window['va'])
            self.windows.append(window)
        # 0x4000 also enters the collision updater's nonzero-flags prefix.
        # Newly saved IDA evidence proves it only writes lastUpdate then
        # returns: (0x4000 & 0x33020) == 0. Do not allow the trace body.
        self.functions = [(begin, 0x1de2240 if begin == 0x1de2210 else end)
                          for begin, end in shared.FUNCTIONS] + [(0x1de1140, 0x1de11aa), (0x1de2f10, 0x1de3425)]
        for begin, end in self.functions:
            if not any(int(w['va'], 16) <= begin and end <= int(w['va'], 16) + w['size'] for w in self.windows):
                raise ValueError('Function lacks verified byte coverage: ' + hex(begin))
        if struct.unpack('<I', self.u.mem_read(0x31a6224 + 0x10, 4))[0] != 0x1de1140:
            raise ValueError('Original finite-frame vtable slot changed')
        finite = verification.parent / 'client-finite-frame-ida.json'
        finite_ida = json.loads(finite.read_text('utf-8-sig'))
        if finite_ida['instruction_count'] != 29 or len(finite_ida['asm']['lines']) != 29:
            raise ValueError('Expected complete 29-instruction finite-frame IDA evidence')
        self.inputs.append(identity(finite))
        self.inputs.append(identity(verification.parent / 'client-draw-ida.json'))
        collision = verification.parent / 'client-collision-branch-ida.json'
        branch = json.loads(collision.read_text('utf-8-sig'))['result'][0]
        if branch['count'] != 14 or branch['matches'][-1]['addr'] != '0x1de223a':
            raise ValueError('Expected complete 14-instruction non-tracing collision prefix')
        self.inputs.extend((identity(collision), identity(verification.parent / 'client-collision-raw-ida.json')))
        self.u.mem_map(shared.PARTICLE, 0x30000)
        self.u.mem_write(shared.TIME_STUB, b'\xdd\x05' + struct.pack('<I', shared.CLOCK) + b'\xc3')
        self.u.mem_write(shared.ANGLE_STUB, b'\xc3')
        self.u.mem_write(0x3ae8e84, struct.pack('<I', shared.TIME_STUB))
        self.u.mem_write(0x3ae8e0c, struct.pack('<I', shared.ANGLE_STUB))
        self.visited, self.calls = set(), dict(time=0, camera=0, angleBasis=0)
        self.active_draw = None
        self.triangle = {}
        table = 0x20023000
        self.u.mem_write(0x3ae8ef4, struct.pack('<I', table))
        for slot, name, count in ((0x2c, 'SpriteTexture', 2), (4, 'RenderMode', 1),
                                  (0x28, 'CullFace', 1), (8, 'Begin', 1),
                                  (0x10, 'Color4f', 4), (0x18, 'TexCoord2f', 2),
                                  (0x1c, 'Vertex3fv', 1), (0xc, 'End', 0)):
            callback = 0x20024000 + slot * 16
            self.triangle[callback] = name, count
            self.u.mem_write(table + slot, struct.pack('<I', callback))
        self.u.hook_add(UC_HOOK_CODE, self.guard)

    def guard(self, u, address, size, _):
        s = self.shared
        if address == 0x2ad39f0:
            esp = u.reg_read(UC_X86_REG_ESP)
            ret, destination = struct.unpack('<II', u.mem_read(esp, 8))
            u.mem_write(destination, struct.pack('<3f', 27, -49, 0))
            u.reg_write(UC_X86_REG_EAX, destination)
            u.reg_write(UC_X86_REG_ESP, esp + 4)
            u.reg_write(UC_X86_REG_EIP, ret)
            self.calls['camera'] += 1
            return
        if address in (s.TIME_STUB, s.TIME_STUB + 6):
            if address == s.TIME_STUB:
                self.calls['time'] += 1
            return
        if address == s.ANGLE_STUB:
            self.calls['angleBasis'] += 1
            if self.active_draw is not None:
                esp = u.reg_read(UC_X86_REG_ESP)
                _, angles, forward, right, up = struct.unpack('<5I', u.mem_read(esp, 20))
                self.active_draw['angles'].append(list(struct.unpack('<3f', u.mem_read(angles, 12))))
                for destination, values in zip((forward, right, up), self.active_draw['basis']):
                    u.mem_write(destination, struct.pack('<3f', *values))
            return
        if address in self.triangle:
            if self.active_draw is None:
                raise AssertionError('Unexpected draw callback during particle update')
            esp = u.reg_read(UC_X86_REG_ESP)
            ret = struct.unpack('<I', u.mem_read(esp, 4))[0]
            name, count = self.triangle[address]
            raw = bytes(u.mem_read(esp + 4, count * 4))
            if name in ('Color4f', 'TexCoord2f'):
                values = list(struct.unpack('<' + 'f' * count, raw))
            else:
                values = list(struct.unpack('<' + 'I' * count, raw))
            if name == 'Vertex3fv':
                values = list(struct.unpack('<3f', u.mem_read(values[0], 12)))
            self.active_draw['commands'].append(dict(operation=name, values=values))
            u.reg_write(UC_X86_REG_EAX, 1)  # Explicit successful SpriteTexture fixture.
            u.reg_write(UC_X86_REG_ESP, esp + 4)
            u.reg_write(UC_X86_REG_EIP, ret)
            return
        if not any(begin <= address and address + size <= end for begin, end in self.functions):
            raise AssertionError(f'Unexpected original instruction {address:#x} size {size}')
        self.visited.add(address)

    def run(self, values):
        if len(values) != len(INPUT_FIELDS) or any(not math.isfinite(v) for v in values):
            raise ValueError('Need exactly 19 finite particle input fields')
        f32, u, s = self.f32, self.u, self.shared
        born, die, size, growth, alpha, ox, oy, oz, vx, vy, vz, now, previous, gravity, rotation, rotate, fps, frames, frame = values
        if alpha > 240 or growth != 2 or fps not in (16, 17) or (fps, frames) not in ((16, 29), (17, 17)):
            raise ValueError('Input outside supported spawn spark fields')
        u.mem_write(s.PARTICLE, bytes(0x200))
        for offset, value in ((0, 0x31a6224), (0x14, 0x4000), (0x84, fps), (0x88, frames), (0x8c, frame)):
            u.mem_write(s.PARTICLE + offset, struct.pack('<I', int(value)))
        for offset, value in ((0x50, size), (0x54, growth), (0x64, alpha), (0x68, .1),
                              (0x6c, born), (0x70, die), (0x74, .1),
                              (0x94, ox), (0x98, oy), (0x9c, oz), (0xa8, rotation),
                              (0xb4, rotate), (0xb8, vx), (0xbc, vy), (0xc0, vz),
                              (0x128, 180), (0x134, 1), (0x138, 240)):
            u.mem_write(s.PARTICLE + offset, struct.pack('<f', value))
        u.mem_write(0x3634978, struct.pack('<f', previous))
        u.mem_write(0x3c4d760, struct.pack('<f', gravity))
        u.mem_write(s.CLOCK, struct.pack('<d', now))
        u.reg_write(UC_X86_REG_ECX, s.PARTICLE)
        u.reg_write(UC_X86_REG_ESP, s.STACK)
        u.mem_write(s.STACK, struct.pack('<If', s.STOP, now))
        u.emu_start(0x2ad3f10, s.STOP, count=5000)
        if u.reg_read(UC_X86_REG_EIP) != s.STOP or u.reg_read(UC_X86_REG_ESP) != s.STACK + 8:
            raise AssertionError('Original instruction budget or callee-clean stack mismatch')
        def read(offset):
            return struct.unpack('<f', u.mem_read(s.PARTICLE + offset, 4))[0]
        result_die = read(0x70)
        result = [int(result_die == 0 or now < result_die), read(0x50), read(0x64), read(0xa8), result_die,
                  *(read(offset) for offset in (0x94, 0x98, 0x9c, 0xb8, 0xbc, 0xc0)),
                  struct.unpack('<i', u.mem_read(s.PARTICLE + 0x8c, 4))[0], read(0x6c), read(0x54), read(0xb4)]
        if read(0xa0) != 27 or read(0xa4) != -49:
            raise AssertionError('Original camera field contract changed')
        if read(0x148) != now:
            raise AssertionError('Original non-tracing collision prefix must record now')
        return [f32(v) for v in result]

    def draw(self, case):
        """Execute base Draw; engine AngleVectors/TriAPI remain fixture edges."""
        u, s = self.u, self.shared
        u.mem_write(s.PARTICLE, bytes(0x200))
        for offset, value in ((8, 0x1c), (0x8c, case['frame']), (0x90, 5), (0xf8, 0x1234)):
            u.mem_write(s.PARTICLE + offset, struct.pack('<I', value))
        for offset, value in ((0x50, case['size']), (0x5c, 1), (0x60, 1),
                              (0x64, case['alpha']), (0x70, case['die']),
                              (0x94, case['origin'][0]), (0x98, case['origin'][1]), (0x9c, case['origin'][2]),
                              (0xa0, 27), (0xa4, -49), (0xa8, case['rotation']),
                              (0xe8, case['color'][0]), (0xec, case['color'][1]), (0xf0, case['color'][2])):
            u.mem_write(s.PARTICLE + offset, struct.pack('<f', value))
        u.mem_write(s.CLOCK, struct.pack('<d', case['now']))
        self.active_draw = dict(basis=case['basis'], angles=[], commands=[])
        u.reg_write(UC_X86_REG_ECX, s.PARTICLE)
        u.reg_write(UC_X86_REG_ESP, s.STACK)
        u.mem_write(s.STACK, struct.pack('<I', s.STOP))
        u.emu_start(0x1de2f10, s.STOP, count=2000)
        if u.reg_read(UC_X86_REG_EIP) != s.STOP or u.reg_read(UC_X86_REG_ESP) != s.STACK + 4:
            raise AssertionError('Original draw budget/stack mismatch')
        result = self.active_draw
        self.active_draw = None
        if case['die'] == case['now']:
            if result['commands'] or result['angles']:
                raise AssertionError('Original draw at die==now must emit no commands')
        else:
            names = [command['operation'] for command in result['commands']]
            expected = ['SpriteTexture', 'RenderMode', 'CullFace', 'Begin', 'Color4f']
            expected += ['TexCoord2f', 'Vertex3fv'] * 4
            expected += ['End', 'RenderMode', 'CullFace']
            if names != expected or result['angles'] != [[27., -49., case['rotation']]]:
                raise AssertionError('Original draw callback sequence/angles changed')
            if result['commands'][0]['values'] != [0x1234, case['frame']]:
                raise AssertionError('Original SpriteTexture frame must pass through unchanged')
        return result


def draw_cases(f32):
    bases = (((1., 0., 0.), (0., -1., 0.), (0., 0., 1.)),
             ((0., 0., 1.), (.6, -.8, 0.), (.8, .6, 0.)))
    for origin in ((0., 0., 0.), (16384.25, -9000.125, 64.)):
        for size in (0., 180., 287.5):
            for alpha in (0., 1., 240.):
                for rotation in (1., 23.125):
                    for color in ((70., 123., 250.), (250., 70., 171.)):
                        for frame in (0, 17, 29):
                            for basis in bases:
                                for now in (100., 101.8):
                                    yield dict(origin=list(origin), size=size, alpha=alpha, rotation=rotation,
                                               color=list(color), frame=frame, now=f32(now), die=f32(101.8),
                                               basis=[[f32(v) for v in axis] for axis in basis])


def cases(f32):
    for born in (1., 100., 4096.):
        for fps, frames in ((16, 29), (17, 17)):
            frame_end = f32(born + f32(frames / fps))
            die = f32(born + f32(1.8))
            times = {f32(born + age) for age in (0, .0001, .016, .1, .5, 1., 1.7, 1.75, 1.79, 1.8, 2., 4., 1200.)}
            for boundary in (frame_end, f32(die - .5), f32(die - .1), die):
                times.update((adjacent(boundary, -1), boundary, adjacent(boundary, 1)))
            for byte in range(1, 6):
                for pose, velocity in (((0., 0., 0.), (0., 0., 200.)),
                                       ((16384.25, -9000.125, 64.), (17.125, -31.75, -200.5))):
                    for gravity in (0., 800., 1000.):
                        for now in sorted(times):
                            for dt in (0., .03125, .5):
                                # A particle can be born after the manager's
                                # previous frame; original motion uses that
                                # global directly without clamping to born.
                                previous = f32(now - dt)
                                row = [born, die, 180., 2., 240., *pose, *velocity,
                                       now, previous, gravity, 1., f32(byte * f32(.1)), fps, frames, 0]
                                yield dict(kind='independent', input=[f32(v) for v in row])


def chains(original):
    """Carry forward actual original outputs while the particle is alive."""
    f32 = original.f32
    for born in (1., 100., 4096.):
        for fps, frames in ((16, 29), (17, 17)):
            for byte in range(1, 6):
                for gravity in (0., 800.):
                    die = f32(born + f32(1.8))
                    frame_end = f32(born + f32(frames / fps))
                    times = {born, f32(born + .016), f32(born + .5), f32(born + .99),
                             frame_end, adjacent(frame_end, 1), f32(born + 1.799), die}
                    row = [born, die, 180., 2., 240., 16384.25, -9000.125, 64.,
                           17.125, -31.75, 200.5, born, f32(born - .03125), gravity,
                           1., f32(byte * f32(.1)), fps, frames, 0]
                    for now in sorted(times):
                        row[11] = now
                        output = original.run(row)
                        yield dict(kind='chain', input=list(row), original=output)
                        if not output[0]:
                            break
                        row[1:5] = output[4], output[1], output[13], output[2]
                        row[5:11] = output[5:11]
                        row[12], row[14], row[18] = now, output[3], output[11]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--client-dll', type=Path)
    parser.add_argument('--verification', type=Path)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--draw-only', action='store_true',
                        help='Record base Draw commands with explicit engine basis/TriAPI fixtures')
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    root = args.source_root.resolve(strict=True)
    client = args.client_dll or root / 'analysis/csol-model-20261017/samples/client.dll'
    verification = args.verification or root / 'analysis/teddy-spawn-visuals-20261111/client-verification.json'
    original = Original(root, client, verification)
    if args.draw_only:
        rows = [dict(input=case, original=original.draw(case)) for case in draw_cases(original.f32)]
        failures, compared, executable = [], 0, None
        if args.executable:
            executable = identity(args.executable)
            original.inputs.append(executable)
            live = [row for row in rows if row['original']['commands']]
            lines = []
            for row in live:
                case = row['input']
                values = [*case['origin'], case['size'], *case['basis'][1], *case['basis'][2]]
                lines.append(' '.join(format(v, '.9g') for v in values) + '\n')
            result = subprocess.run([str(args.executable.resolve()), '--geometry-oracle'], input=''.join(lines),
                                    text=True, capture_output=True, timeout=60, check=True)
            actual = [json.loads(line) for line in result.stdout.splitlines()]
            if len(actual) != len(live):
                raise AssertionError(('Production geometry row count', len(actual), len(live)))
            for row, values in zip(live, actual):
                if not isinstance(values, list) or len(values) != 12 or any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
                    raise AssertionError('Production geometry must return exactly 12 finite coordinates')
                expected = [value for command in row['original']['commands'] if command['operation'] == 'Vertex3fv' for value in command['values']]
                if len(expected) != 12:
                    raise AssertionError('Original must emit four XYZ vertices')
                row['productionCorners'] = values
                if any(struct.pack('<f', a) != struct.pack('<f', b) for a, b in zip(expected, values)):
                    failures.append(row)
                compared += 1
        unchanged = all(identity(Path(row['path'])) == row for row in original.inputs)
        report = dict(success=unchanged and not failures, inputsUnchanged=unchanged, inputs=original.inputs,
                      originalOnly=executable is None, executable=executable,
                      originalDrawExecuted=True, samples=len(rows), rows=rows,
                      callbackBoundaries=dict(engineTime='controlled float64 return',
                                              angleVectors='explicit orthonormal basis, angles recorded',
                                              triangleApi='recorded cdecl callbacks; SpriteTexture fixture returns1'),
                      fixedFields=dict(drawFlags=0x1c, aspect=[1, 1], mode=5), windows=original.windows,
                      executedInstructionAddresses=[hex(v) for v in sorted(original.visited)],
                      productionCompared=executable is not None, productionComparison='Corners geometry only; renderer callback sequence/color/UV not compared',
                      comparedGeometryCases=compared, failureCount=len(failures), failures=failures,
                      deployed=False, liveGameplay=False)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
        print(json.dumps({k: report[k] for k in ('success', 'inputsUnchanged', 'originalOnly', 'originalDrawExecuted', 'samples', 'comparedGeometryCases', 'failureCount')}))
        return 0 if report['success'] else 1
    rows = []
    equal_boundaries, greater_boundaries = 0, 0
    for row in cases(original.f32):
        row['original'] = original.run(row['input'])
        values, output = row['input'], row['original']
        computed = original.f32(original.f32(values[11] - values[0]) * values[16])
        if computed == values[17]:
            if output[4] != values[1] or output[11] != values[17]:
                raise AssertionError('Original ==frames must preserve death time and retain frame')
            equal_boundaries += 1
        elif computed > values[17] and values[11] < values[1]:
            if output[4] != values[11]:
                raise AssertionError('Original >frames before nominal life must set die=now')
            greater_boundaries += 1
        rows.append(row)
    rows.extend(chains(original))
    if not rows or not equal_boundaries or not greater_boundaries or 0x1de1140 not in original.visited or 0x1de10f0 in original.visited:
        raise AssertionError('Finite-animation equality and strict-greater branches must execute')
    failures = []
    executable = None
    if args.executable:
        executable = identity(args.executable)
        original.inputs.append(executable)
        payload = ''.join(' '.join(format(v, '.9g') for v in row['input']) + '\n' for row in rows)
        result = subprocess.run([str(args.executable.resolve()), '--particle-oracle'], input=payload,
                                text=True, capture_output=True, timeout=60, check=True)
        actual = [json.loads(line) for line in result.stdout.splitlines()]
        if len(actual) != len(rows):
            raise AssertionError(('Production result row count', len(actual), len(rows)))
        for row, values in zip(rows, actual):
            if not isinstance(values, list) or len(values) != len(OUTPUT_FIELDS):
                raise AssertionError('Production must return exactly 15 fields per row')
            row['production'] = values
            if any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
                raise AssertionError('Production emitted a nonfinite/nonnumeric field')
            if any(struct.pack('<f', expected) != struct.pack('<f', actual) for expected, actual in zip(row['original'], values)):
                failures.append(row)
    unchanged = all(identity(Path(row['path'])) == row for row in original.inputs)
    report = dict(success=unchanged and not failures, inputsUnchanged=unchanged, inputs=original.inputs,
                  clientSha256=original.shared.CLIENT_SHA, executable=executable,
                  originalOnly=executable is None, inputFields=INPUT_FIELDS, outputFields=OUTPUT_FIELDS,
                  samples=len(rows), failureCount=len(failures), failures=failures,
                  finiteEqualCases=equal_boundaries, finiteGreaterBeforeLifeCases=greater_boundaries,
                  windows=original.windows, executedInstructionAddresses=[hex(v) for v in sorted(original.visited)],
                  callbacks=original.calls, rows=rows,
                  boundaries=dict(originalInstructions=True, engineTime='controlled float64 return',
                                  camera='controlled 27/-49/0', angleBasis='controlled no-op; unused with derived flags0',
                                  updateSelectedState=True, managerOrEmitterExecuted=False, originalDrawExecuted=False,
                                  productionCompiled=False, deployed=False, liveGameplay=False))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: report[k] for k in ('success', 'originalOnly', 'samples', 'failureCount', 'finiteEqualCases', 'finiteGreaterBeforeLifeCases', 'inputsUnchanged')}))
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
