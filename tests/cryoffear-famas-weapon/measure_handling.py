# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure native FAMAS samples with CoF's original seeded rays and MP view feedback."""
import argparse
import csv
from functools import lru_cache
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess

TESTS = Path(__file__).resolve().parent.parent
DISTANCE = 1024


def oracle(suite):
    spec = importlib.util.spec_from_file_location(suite, TESTS / suite / 'oracle.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_shots(path):
    with path.open(newline='') as file:
        rows = list(csv.DictReader(file))
    groups = {}
    for row in rows:
        key = (row['pose'], int(row['aim']), int(row['material']))
        groups.setdefault(key, []).append(row)
    poses = ('standing', 'walking', 'crouching', 'crouch-walk', 'airborne')
    if set(groups) != {(pose, aim, material) for pose in poses for aim in (0, 1) for material in (0, 3)}:
        raise ValueError('Missing stance, aim mode or ballistic backend')
    result = {}
    for pose in poses:
        for aim in (0, 1):
            baseline = groups[pose, aim, 0]
            material = groups[pose, aim, 3]
            if len(baseline) != 30 or [int(r['shot']) for r in baseline] != list(range(1, 31)):
                raise ValueError('Incomplete magazine')
            if baseline != [r | {'material': '0'} for r in material]:
                raise ValueError('The two authoritative bullet consumers received different inputs')
            result[pose, aim] = baseline
    return result


def peak_view(rows, client):
    by_tick = {int(r['tick']): tuple(float(r[k]) for k in ('pitch', 'yaw', 'roll')) for r in rows}
    state = (0.0,) * 6
    peak = [0.0] * 3
    for tick in range(max(by_tick) + 101):
        # Original cooperative view event and CalcRefdef spring, for this
        # recorded impulse sequence at 100 Hz. Other camera effects are separate.
        impulse = by_tick.get(tick, (0.0, 0.0, 0.0))
        sample = (*state, *impulse, 0.0, 0.0, 0.0, .01, 32, 0, 0)
        output = struct.unpack('<9f2i', client.run(sample))
        state = output[:6]
        peak = [max(previous, abs(current)) for previous, current in zip(peak, output[6:9])]
    return dict(zip(('pitch', 'yaw', 'roll'), peak))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('samples', 'mp', 'client', 'ballistics-runner', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    shots = load_shots(args.samples)
    bullets = oracle('cryoffear-ballistics')
    mp = bullets.Original(args.mp)
    client = oracle('cryoffear-view-punch').Original(args.client)
    bit = bullets.bits
    endpoints = []

    @lru_cache(maxsize=8192)
    def radius(cone, aim, seed):
        # World axis X is the bore. Read the trace endpoint emitted by the
        # unmodified FireBullets code and intersect that ray with the wall.
        row = [0, 0, 0, bit(1), 0, 0, cone, cone, cone, 0, bit(1), 0,
               0, 0, bit(1), seed, 15, aim, 0, 2, 0, 0]
        _, events = mp.run(row)
        at = 0
        while at < len(events):
            kind, size = events[at:at+2]
            if kind == 2:
                endpoint = events[at+6:at+9]
                x, y, z = map(bullets.number, endpoint)
                if size != 7 or x != 8192:
                    raise ValueError('Unexpected original trace endpoint')
                endpoints.append(' '.join(map(str, (cone, aim, seed, *endpoint))))
                return DISTANCE * math.hypot(y, z) / x
            at += 2 + size
        raise ValueError('Original bullet did not trace')

    report = {}
    for (pose, aim), magazine in shots.items():
        results = {}
        for count in (1, 3, 30):
            rows = magazine[:count]
            radii = sorted(radius(int(row['cone']), aim, (int(row['seed'])+offset) & 255)
                           for row in rows for offset in range(256))
            results[count] = {
                'shots': len(radii),
                'radiusP90': radii[math.ceil(.9*len(radii))-1],
                'peakViewDegrees': peak_view(rows, client),
            }
        report[pose + ('-ads' if aim else '-hip')] = results
    checked = subprocess.run([str(args.ballistics_runner.resolve()), '--rays'],
                             input='\n'.join(endpoints)+'\n', text=True, capture_output=True,
                             check=True, timeout=60)
    if checked.stdout.strip() != f'{len(endpoints)} original and CS rifle SDK ray pairs matched':
        raise ValueError('Incomplete native ray comparison')
    args.output.write_text(json.dumps(dict(distance=DISTANCE, tickHz=100,
        initialSeeds=256, originalRayExecutions=radius.cache_info().misses, verifiedSdkRayPairs=len(endpoints),
        results=report), indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
