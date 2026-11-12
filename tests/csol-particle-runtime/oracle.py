#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the actual C++ ordinary owner to unchanged original instructions."""
import argparse
import json
import random
import struct
import subprocess
from pathlib import Path

from originals import Original, bits, identity


def particle(identifier, *, visible=1, die=20., origin=None, move=None, after=None):
    return dict(id=identifier, visible=visible, die=die,
                origin=origin or [float(identifier * 3), 0., 0.], move=move or [0., 0., 0.],
                changeDie=after is not None, afterDie=after or 0.)


def case(label, particles, *, previous=9., clocks=None, camera=None):
    return dict(label=label, previous=previous, particles=particles, clocks=clocks or [10.],
                camera=camera or [0., 0., 0.])


def cases():
    out = [case('empty', []), case('advance', [particle(i) for i in range(1, 4)]),
           case('equal', [particle(1), particle(2, visible=0)], previous=10.),
           case('rewind', [particle(1)], clocks=[8.]),
           case('per-object resample', [particle(i) for i in range(1, 4)], clocks=[10., 9., 11., 9.]),
           case('die equal', [particle(1, die=10.)]),
           case('zero lifetime', [particle(1, die=0.)]),
           case('expired invisible', [particle(1, die=8., visible=0)]),
           case('update changes expiry', [particle(1, die=20., after=10.)]),
           case('update cancels expiry', [particle(1, die=8., after=0.)]),
           case('sort before update', [particle(1, move=[100., 0., 0.]), particle(2)]),
           case('invisible still updates', [particle(1, visible=0, move=[2., 3., 4.])]),
           case('snapshot differs from sample', [particle(1), particle(2)], clocks=[10., 12., 13.]),
           case('equal snapshot but unequal sample', [particle(1)], previous=10., clocks=[10., 11.]),
           case('unequal snapshot but equal sample', [particle(1)], clocks=[10., 9.]),
           case('negative time', [particle(1, die=-2.)], previous=-4., clocks=[-3.]),
           case('nan time', [particle(1)], clocks=[float('nan')]),
           case('infinite time', [particle(1), particle(2, die=0.)], clocks=[float('inf')]),
           case('nan lifetime', [particle(1, die=float('nan'))])]
    rng = random.Random(0x1de4510)
    for index in range(80):
        rows = []
        for i in range(rng.randrange(1, 13)):
            rows.append(particle(i + 1, visible=rng.randrange(2),
                                  die=rng.choice([0., 8., 9., 10., 11., 20.]),
                                  origin=[float((i + 1) * 11), float(i * i), float(i % 3)],
                                  move=[rng.choice([-3., 0., 7.]), .25, -.5],
                                  after=rng.choice([None, None, 0., 9., 12.])))
        out.append(case('mixed-' + str(index), rows, previous=rng.choice([9., 10., 11.]),
                        clocks=[rng.choice([8., 9., 10., 11.]) for _ in range(len(rows) + 1)],
                        camera=[.5, -2., 4.]))
    return out


def encode(case):
    fields = [len(case['particles']), bits(case['previous']), *(bits(x) for x in case['camera']),
              len(case['clocks']), *(bits(x) for x in case['clocks'])]
    for p in case['particles']:
        fields += [p['id'], p['visible'], bits(p['die']), *(bits(x) for x in p['origin']),
                   *(bits(x) for x in p['move']), int(p['changeDie']), bits(p['afterDie'])]
    return ' '.join(map(str, fields))


def execute(exe, mode, lines):
    p = subprocess.run([str(exe), mode], input='\n'.join(lines) + '\n', capture_output=True,
                       text=True, encoding='utf8', timeout=60)
    if p.returncode:
        raise RuntimeError(p.stderr or p.stdout)
    return [json.loads(line) for line in p.stdout.splitlines()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'client', 'evidence', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    a = parser.parse_args()
    original = Original(a.client, a.evidence)
    inputs = [*original.inputs, identity(a.executable),
              *(identity(p) for p in Path(__file__).parent.glob('*.py'))]
    fixtures = cases()
    actual = execute(a.executable, 'frame', [encode(row) for row in fixtures])
    assert len(actual) == len(fixtures)
    frames = []
    for fixture, got in zip(fixtures, actual):
        expected = original.frame(fixture)
        assert got == expected, (fixture['label'], got, expected)
        frames.append(dict(label=fixture['label'], input=fixture, result=expected))

    camera_rows = [(now, previous) for now in (10., 8., -1., float('nan'))
                   for previous in (9., 10.)]
    camera_input = [' '.join(map(str, [bits(previous), bits(now), bits(800.),
                                     *(bits(x) for x in (4., 5., 6.))]))
                    for now, previous in camera_rows]
    camera_actual = execute(a.executable, 'camera', camera_input)
    camera_expected = [original.camera(previous, now, 800., [4., 5., 6.])
                       for now, previous in camera_rows]
    assert camera_actual == camera_expected

    values = [bits(v) for v in (0., -0., 9., 25., -25., float('inf'), -float('inf'))]
    values += [0x7fc00000, 0xffc00000, 0x7fc00001]
    pairs = [(x, y) for x in values for y in values]
    comparator_actual = execute(a.executable, 'compare', [f'{x} {y}' for x, y in pairs])
    comparator_expected = [original.compare(x, y) for x, y in pairs]
    assert comparator_actual == comparator_expected

    # Ordinary allocation executes the real cap/gate/free-head branches over
    # a declared prepared 10000-node fixture. No allocator is mocked here.
    original.configure(9.)
    original.word(0x340e650, 0)
    assert original.call(0x1de4d40, (0x1e0,)) == 0
    original.word(0x340e650, 1)
    addresses = [original.call(0x1de4d40, (0x1e0,)) for _ in range(1200)]
    assert all(addresses) and len(set(addresses)) == 1200
    assert original.call(0x1de4d40, (0x1e0,)) == 0
    assert original.integer(0x600010) == 1200
    original.call(0x1de4f10)
    assert original.integer(0x600010) == 0 and original.integer(0x3634978) == bits(9.)
    assert original.call(0x1de4d40, (0x1e0,)) == addresses[0]
    unchanged = all(identity(Path(x['path'])) == x for x in inputs)
    assert unchanged, 'Oracle input changed'
    report = dict(success=True, frameCases=frames, cameraCases=len(camera_rows),
                  comparatorCases=len(pairs), ordinaryAllocation=dict(nodes=10000, cap=1200,
                  gate=True, freeHeadReuse=True, clearRetainsPreviousTime=True),
                  originalIdaStarts=original.starts, originalRawWindows=original.windows,
                  executedOriginalStarts=len(original.executed), inputs=inputs, inputsUnchanged=unchanged,
                  boundaries=dict(particleVirtuals='controlled visibility/update/retire/draw and live origin getter',
                  time='controlled per-call f32 values; actual x87 return ABI',
                  crt='controlled qsort at original comparator address; frame fixtures have distinct finite sort keys; comparator instructions compared separately',
                  timing='controlled QPF/QPC/int64 conversion; not a performance measurement',
                  pool='10000 prepared nodes; original cold allocation constructors are statically verified, not replayed',
                  renderer='ordinary manager only; no enhanced renderer, actual GL, auxiliary lists or plugin callbacks'),
                  runtimeIntegrated=False, deployed=False, liveGameplay=False, performanceAccepted=False)
    a.output.write_text(json.dumps(report, indent=2) + '\n', 'utf8')
    print(json.dumps(dict(success=True, frames=len(frames), camera=len(camera_rows),
                          comparator=len(pairs), executedOriginalStarts=len(original.executed))))


if __name__ == '__main__':
    main()
