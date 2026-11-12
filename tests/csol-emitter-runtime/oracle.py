#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare production C++ owner to unchanged original emitter instructions."""
import argparse
import json
import math
import random
import subprocess
from pathlib import Path

from originals import Original, bits, identity


def record(identifier=1, **changes):
    row = dict(tag=43, total=3, burst=1, remaining=3, active=1, interval=.1,
               next=9., model=1, id=identifier, kind=identifier % 3, retained=17)
    row.update(changes)
    return row


def case(label, records=None, **changes):
    row = dict(label=label, records=records or [], now=10., cached=8., afterLoad=9.,
               model=1, failAt=-1, input=record(999, remaining=99, next=1.))
    row.update(changes)
    return row


def encode(row, mode):
    fields = [len(row['records']), *(bits(row[x]) for x in ('now', 'cached', 'afterLoad')),
              row['model'], row['failAt']]
    for r in row['records'] + ([row['input']] if mode == 'add' else []):
        fields += [*(r[x] for x in ('tag', 'total', 'burst', 'remaining', 'active')),
                   bits(r['interval']), bits(r['next']),
                   *(r[x] for x in ('model', 'id', 'kind', 'retained'))]
    return ' '.join(map(str, fields))


def fixtures():
    add = [case('empty Add'), case('reuse by remaining ignores active', [record(1, remaining=0, active=2)]),
           case('negative remaining is occupied', [record(1, remaining=-1)]),
           case('null model still activates', model=0),
           case('full first-free scan', [record(i + 1) for i in range(128)]),
           case('last slot free', [record(i + 1, remaining=0 if i == 127 else 1) for i in range(128)]),
           case('first zero total reusable', [record(1, remaining=0, total=0)]),
           case('resources change cached time', afterLoad=17.)]
    for total in (-2, -1, 0, 1, 2147483647):
        add.append(case(f'total-{total}', input=record(999, total=total, remaining=99)))
    for requested in (0., -0., -10., 9., 10., 20., float('nan'), float('inf')):
        for cache in (9., float('nan')):
            add.append(case(f'Add clocks {requested}/{cache}', afterLoad=cache,
                            input=record(999, next=requested, remaining=99)))

    frame = [case('empty Frame'), case('ordinary', [record(i) for i in range(1, 4)]),
             case('active is not eligibility', [record(1, active=0), record(2, active=2)]),
             case('zero next disabled', [record(1, next=0.), record(2, next=-0.), record(3)]),
             case('same clock disabled', [record(1, next=10.)]),
             case('rewind before next', [record(1)], now=8.),
             case('negative clock', [record(1, next=-3.)], now=-2.),
             case('bad sprite consumes then continues', [record(1, model=0), record(2)]),
             case('factory fails first', [record(1), record(2)], failAt=0),
             case('factory fails mid burst', [record(1, burst=8), record(2)], failAt=3),
             case('factory fails second record', [record(1), record(2), record(3)], failAt=1),
             case('one count per burst', [record(1, burst=5)]),
             case('remaining consumed to zero', [record(1, remaining=1)]),
             case('infinite negative count', [record(1, remaining=-1, total=-1)]),
             case('infinite zero count', [record(1, remaining=0, total=-1)]),
             case('infinite positive count decrements', [record(1, remaining=1, total=-1)]),
             case('other negative total does not run', [record(1, remaining=-1, total=-2)]),
             case('all 128 share phase', [record(i + 1) for i in range(128)])]
    for burst in (-2147483648, -1, 0, 1, 2, 99, 100, 101, 2147483647):
        frame.append(case(f'burst-{burst}', [record(1, burst=burst)]))
    for now in (0., -0., float('nan'), float('inf'), -float('inf')):
        frame.append(case(f'now-{now}', [record(1)], now=now))
    for next_time in (float('nan'), float('inf'), -float('inf')):
        frame.append(case(f'next-{next_time}', [record(1, next=next_time)]))
    rng = random.Random(0x2aa6650)
    for index in range(90):
        rows = [record(i + 1, total=rng.choice([-2, -1, 0, 5]),
                       remaining=rng.choice([-1, 0, 1, 4]), active=rng.choice([0, 1, 2]),
                       burst=rng.choice([-1, 0, 1, 2, 3, 101]), model=rng.randrange(2),
                       next=rng.choice([-2., 0., 8., 9., 10., 11.]),
                       interval=rng.choice([0., -.1, .09, .1, 2.]))
                for i in range(rng.randrange(1, 9))]
        frame.append(case(f'mixed-{index}', rows, failAt=rng.choice([-1, 0, 2, 7]),
                          now=rng.choice([-1., 0., 9., 10., 11.])))
    video = [case('empty video'), case('all video states', [record(i + 1, active=i % 4 - 1,
                   total=-1 if i % 2 else 3, remaining=-1 if i % 2 else 3) for i in range(128)])]
    for active in (-1, 0, 1, 2, 2147483647):
        video.append(case(f'video-active-{active}', [record(1, active=active)]))
    reset = [case('empty Reset'), case('full Reset', [record(i + 1, active=i % 3) for i in range(128)])]
    return dict(add=add, frame=frame, video=video, reset=reset)


def json_safe(value):
    if isinstance(value, float) and not math.isfinite(value):
        return {'float32Bits': f'{bits(value):08x}'}
    if isinstance(value, dict):
        return {key: json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'client', 'evidence', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    a = parser.parse_args()
    original = Original(a.client, a.evidence)
    inputs = [*original.inputs, identity(a.executable),
              *(identity(p) for p in Path(__file__).parent.glob('*.py'))]
    results = {}
    for mode, cases in fixtures().items():
        execution = subprocess.run([str(a.executable), mode],
                    input='\n'.join(encode(x, mode) for x in cases) + '\n',
                    capture_output=True, text=True, encoding='utf8', timeout=90)
        assert execution.returncode == 0, execution.stderr
        actual = [json.loads(line) for line in execution.stdout.splitlines()]
        assert len(actual) == len(cases)
        results[mode] = []
        for fixture, got in zip(cases, actual):
            expected = original.run(mode, fixture)
            assert got == expected, (mode, fixture['label'], got, expected)
            results[mode].append(dict(label=fixture['label'], input=fixture, result=expected))
    unchanged = all(identity(Path(x['path'])) == x for x in inputs)
    assert unchanged, 'Oracle input changed'
    report = dict(success=True, cases=results, originalIdaStarts=original.starts,
                  originalRawWindows=original.windows, executedOriginalStarts=len(original.executed),
                  inputs=inputs, inputsUnchanged=unchanged,
                  boundaries=dict(resources='controlled LoadSprite/GetModel with cache mutation',
                  factory='controlled successful blank objects or selected failure; original post-factory initializer and scheduler execute',
                  particle='only owner state/events compared; particle algorithms separately tested',
                  owner='world owner0 only; unknown entity interface is not substituted',
                  video='temporary record subset compared; original full routine/resource requests also execute',
                  clocks='controlled f32/x87 time and actual cached HUD global; no performance claim'),
                  runtimeIntegrated=False, deployed=False, liveGameplay=False, performanceAccepted=False)
    a.output.write_text(json.dumps(json_safe(report), indent=2, allow_nan=False) + '\n', 'utf8')
    print(json.dumps(dict(success=True, cases={k: len(v) for k, v in results.items()},
                          executedOriginalStarts=len(original.executed))))


if __name__ == '__main__':
    main()
