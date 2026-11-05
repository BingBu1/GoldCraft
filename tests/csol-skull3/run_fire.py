#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual SKULL3 header and compare external original-instruction results."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('oracle', 'output', 'environment', 'compiler'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir(parents=True)
    (out / 'temp').mkdir()
    oracle = json.loads(args.oracle.read_text('utf8'))
    assert oracle['moduleSha256'] == '39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2'
    rows = []
    for row in oracle['fire']['cases']:
        dual = row['kind'] == 'd'
        state = row['input']
        actual = row['output']
        rows.append(['A', int(dual), actual['shots'], bits(actual['accuracy'])])
        kick = next(event[2] for event in actual['events'] if event[0] == 'kickvector')
        rows.append(['R', int(dual), int(bool(state['flags'] & 0x200)),
                     int(any(state['vel'])), int(bool(state['flags'] & 0x4000)),
                     *[bits(value) for value in kick[:6]], int(kick[6])])
    for row in oracle['fire']['primaryCases']:
        rows.append(['S', int(row['kind'] == 'd'), bits(row['oldAccuracy']),
                     int(bool(row['flags'] & 0x200)), int(row['airTime'] > 100),
                     row['stateByte'], bits(row['spread'])])
    for row in oracle['extraAccuracy']:
        rows.append(['A', int(row['dual']), row['shots'], row['accuracyBits']])
    for kind, record in oracle['properties'].items():
        properties = {item['name']: item for item in record['properties']}
        values = [properties[key]['value'] for key in ('CycleTime', 'ReloadTime',
                  'RangeMod', 'AccuracyAdd', 'AccuracyLimit', 'SpreadStand')]
        rows.append(['P', int(kind == 'd'), *map(bits, values),
                     int(properties['Penetration']['value']) if kind == 'd' else 1])
    payload = ''.join(' '.join(map(str, row)) + '\n' for row in rows)
    (out / 'input.txt').write_text(payload, encoding='utf8')
    inputs = []
    for relative in ('native/csol/skull3.hpp', 'tests/csol-skull3/fire.cpp'):
        source = ROOT / relative
        frozen = out / 'source' / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        frozen.write_bytes(source.read_bytes())
        inputs.append(dict(original=identity(source), frozen=identity(frozen)))
    baseline = json.loads(args.environment.read_text('utf-8-sig'))
    environment = os.environ | baseline.get('environment', baseline)
    environment['TEMP'] = environment['TMP'] = str(out / 'temp')
    compiler = str(args.compiler.resolve())
    command = [compiler, '--target=i686-pc-windows-msvc', '/std:c++20', '/clang:-O3',
               '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '/MT', '-fuse-ld=lld',
               str(out / 'source/tests/csol-skull3/fire.cpp'), '/Fo' + str(out / 'fire.obj'),
               '/Fe' + str(out / 'fire.exe'), '/link', '/OPT:LLDLTO=3', '/threads:1']
    start = time.monotonic()
    build = subprocess.run(command, cwd=ROOT, env=environment, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=90)
    (out / 'build.log').write_bytes(build.stdout)
    assert build.returncode == 0, build.stdout.decode('utf8', 'replace')
    run = subprocess.run([str(out / 'fire.exe')], input=payload.encode(),
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    (out / 'run.log').write_bytes(run.stdout)
    result = json.loads(run.stdout)
    unchanged = all(identity(item[part]['path'])['sha256'] == item[part]['sha256']
                    for item in inputs for part in ('original', 'frozen'))
    receipt = dict(success=run.returncode == 0 and unchanged, result=result,
                   inputs=inputs, oracle=identity(args.oracle), compiler=identity(compiler),
                   command=command, buildExit=build.returncode, runExit=run.returncode,
                   unchanged=unchanged, seconds=time.monotonic() - start, liveGameplay=False)
    (out / 'result.json').write_text(json.dumps(receipt, indent=2), encoding='utf8')
    print(json.dumps({key: value for key, value in receipt.items()
                      if key not in ('command', 'inputs')}, indent=2))
    assert receipt['success']


if __name__ == '__main__':
    main()
