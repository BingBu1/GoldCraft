#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the actual SKULL2 core against previously captured original results."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def identity(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--evidence', type=Path, required=True)
    ap.add_argument('--source-manifest', type=Path, required=True)
    ap.add_argument('--environment', type=Path, required=True)
    ap.add_argument('--compiler', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--extended', type=Path)
    ap.add_argument('--legacy-header', type=Path)
    ap.add_argument('--helpers', type=Path)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir()  # Preserve all earlier candidates, including failed ones.
    source = output / 'source'
    source_manifest = read(args.source_manifest)
    source_rows = source_manifest if isinstance(source_manifest, list) else [source_manifest]
    frozen = next(row for row in source_rows if row['path'] == 'native/csol/skull2.hpp')
    header = root / frozen['path']
    assert identity(header)['sha256'] == frozen['sha256']
    assert header.stat().st_size == frozen['bytes']
    manifest = read(args.evidence / 'manifest.json')
    reference = {Path(row['path']).name: row for row in manifest}
    names = ('state-oracle.json', 'properties.json', 'state_oracle.py', 'contract.md')
    evidence = []
    for name in names:
        actual = identity(args.evidence / name)
        assert actual['sha256'] == reference[name]['sha256'], 'evidence drift: ' + name
        evidence.append(actual)
    oracle = read(args.evidence / 'state-oracle.json')
    properties = read(args.evidence / 'properties.json')
    assert oracle['success'] and oracle['caseCount'] == len(oracle['cases']) == 43
    assert oracle['moduleSha256'] == properties['moduleSha256']
    assert len(properties['properties']) == 62
    damage_checks = 0
    for row in source_rows:
        assert identity(root / row['path'])['sha256'] == row['sha256']
        if row['path'] == 'native/csol/config/skull2_damage.json':
            config = read(root / row['path'])
            values = {prop['name']: prop for prop in properties['properties']}
            assert config.pop('version') == 1 and len(config) == 6
            for name, value in config.items():
                assert struct.unpack('<I', struct.pack('<f', value))[0] == int(values[name]['bits'], 16)
                damage_checks += 1
    rows = []
    for number, case in enumerate(oracle['cases']):
        if 'capacityFlags' in case:
            row = ['C', number, case['capacityFlags'], case['capacity']]
        elif 'mode' in case:
            start, equal, final = (case[key] for key in ('start', 'equalDeadline', 'commit'))
            assert len(start['animations']) == 1
            row = ['S', number, case['mode'], case['clip'], case['reserve'], start['flags'],
                   start['deadline'], start['animations'][0]['sequence'], equal['flags'],
                   equal['clip'], equal['reserve'], final['flags'], final['clip'], final['reserve']]
        elif 'holsterFlags' in case:
            row = ['H', number, case['holsterFlags'], case['result']['flags'],
                   case['result']['clip'], case['result']['reserve']]
        elif 'pendingSecondary' in case:
            assert not case['result']['animations']
            row = ['P', number, case['pendingSecondary'], case['result']['flags'], case['result']['deadline']]
        else:
            raise ValueError('unknown oracle case')
        rows.append(' '.join(map(str, row)))
    (output / 'oracle.txt').write_text('\n'.join(rows) + '\n', encoding='utf-8')
    (output / 'properties.txt').write_text(''.join(
        f"{row['name']} {int(row['bits'], 16)}\n" for row in properties['properties']), encoding='utf-8')
    inputs = []
    for relative in (frozen['path'], 'tests/csol-skull2/core.cpp'):
        destination = source / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / relative, destination)
        assert destination.stat().st_nlink == 1
        inputs += [identity(root / relative), identity(destination)]
    inputs += evidence + [identity(args.source_manifest), identity(args.environment), identity(args.compiler),
                         identity(__file__), identity(output / 'oracle.txt'), identity(output / 'properties.txt')]
    inputs += [identity(root / row['path']) for row in source_rows if row['path'] != frozen['path']]
    extra_options = []
    extra_arguments = []
    if args.extended:
        assert args.legacy_header, 'extended comparison requires the preserved core01 header'
        ext = read(args.extended)
        assert ext['success'] and ext['caseCount'] == len(ext['cases']) == 2592
        assert ext['moduleSha256'] == oracle['moduleSha256'] and not ext['changedInputs']
        for row in ext['inputs']:
            assert identity(row['path']) == row, 'extended original input drift'
        lines = []
        bits = lambda value: struct.unpack('<I', struct.pack('<f', value))[0]
        for row in ext['cases']:
            before, after = row['input'], row['output']
            animations = after['animations']
            assert len(animations) <= 1
            lines.append(' '.join(map(str, [before['operation'], before['flags'], before['clip'], before['reserve'],
                bits(before['now']), bits(before['deadline']), after['flags'], after['clip'], after['reserve'],
                after['deadlineBits'], animations[0]['sequence'] if animations else -1,
                int(before['operation'] == 'F' and after['pmodel'] != 0)])))
        (output / 'extended.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        assert identity(args.legacy_header)['sha256'] == '3f09b2020d081b33202b945975dc3bc01aa11755f15194ca4accf70dbbdbc757'
        old = source / 'tests/csol-skull2/legacy_core.hpp'
        shutil.copy2(args.legacy_header, old)
        inputs += [identity(args.extended), identity(args.legacy_header), identity(old), identity(output / 'extended.txt')]
        extra_options = ['/DSKULL2_EXTENDED=1']
        extra_arguments = [str(output / 'extended.txt')]
    if args.helpers:
        helper = read(args.helpers)
        assert helper['success'] and helper['caseCount'] == len(helper['cases']) == 800
        assert helper['moduleSha256'] == oracle['moduleSha256'] and not helper['changedInputs']
        for row in helper['inputs']:
            assert identity(row['path']) == row, 'helper original input drift'
        lines = []
        bits = lambda value: struct.unpack('<I', struct.pack('<f', value))[0]
        for row in helper['cases']:
            before, after = row['input'], row['output']
            if before['operation'] == 'S':
                values = ['S', before['flags'], int(before['grounded']), int(before['ducking']),
                    bits(before['speed']), int(before['air']), int(before['standing']),
                    after['spreadBits'], after['intervalBits'], after['accuracyBits']]
            else:
                values = ['A', before['flags'], before['shots'], bits(before['previous']), bits(before['now']),
                    int(after['allowed']), after['shots'], after['accuracyBits'], after['lastFireBits']]
            lines.append(' '.join(map(str, values)))
        (output / 'helpers.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        inputs += [identity(args.helpers), identity(output / 'helpers.txt')]
        extra_options += ['/DSKULL2_HELPERS=1']
        extra_arguments += [str(output / 'helpers.txt')]
    environment = read(args.environment)
    if 'environment' in environment:
        environment = environment['environment']
    env = os.environ.copy()
    env.update(environment)
    temporary = output / 'temp'
    temporary.mkdir()
    env.update(TEMP=str(temporary), TMP=str(temporary))
    exe = output / 'skull2-core.exe'
    command = [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20',
               '/EHsc', '/MT', '/utf-8', '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2',
               '/fp:precise', '/showIncludes', '-fuse-ld=lld', *extra_options, '/Fo' + str(output / 'core.obj'),
               '/Fd' + str(output / 'core.pdb'), '/Fe' + str(exe),
               str(source / 'tests/csol-skull2/core.cpp'), '/link', '/OPT:REF',
               '/OPT:LLDLTO=3', '/threads:1', '/opt:lldltojobs=1']
    plan = dict(inputs=inputs, command=command, jobs=1, damageConfigChecks=damage_checks,
                boundary='One actual C++ core TU against saved original instruction results; no SDK, DLL or gameplay.')
    write(output / 'plan.json', plan)
    result = dict(success=False, steps=[], actualIncludes=[], productionEdits=False,
                  boundary=plan['boundary'])
    try:
        for name, argv in [('compile', command),
                           ('test', [str(exe), str(output / 'oracle.txt'), str(output / 'properties.txt'), *extra_arguments])]:
            started = time.monotonic()
            log = output / (name + '.log')
            with log.open('xb') as stream:
                run = subprocess.run(argv, cwd=output, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=120)
            text = log.read_text('utf-8', errors='replace')
            result['steps'].append(dict(name=name, exit=run.returncode, seconds=time.monotonic() - started,
                                        log=identity(log)))
            if name == 'compile':
                result['actualIncludes'] = [identity(path.strip()) for path in re.findall(
                    r'^\s*Note: including file:\s*(.*?)\s*$', text, re.M)]
                expected = identity(source / frozen['path'])
                assert any(row == expected for row in result['actualIncludes']), 'production header not observed'
            else:
                match = re.search(r'checks=(\d+) failures=(\d+) oracleCases=(\d+) propertyValues=(\d+)', text)
                assert match, 'missing test summary'
                result['summary'] = dict(zip(('checks', 'failures', 'oracleCases', 'propertyValues'), map(int, match.groups())))
                assert result['summary']['failures'] == 0
                if args.extended:
                    extended_match = re.search(r'extended cases=(\d+) legacyBegin=(\d+) legacyFrame=(\d+) legacyHolster=(\d+)', text)
                    assert extended_match, 'missing extended summary'
                    result['extended'] = dict(zip(('cases', 'legacyBegin', 'legacyFrame', 'legacyHolster'), map(int, extended_match.groups())))
                if args.helpers:
                    helper_match = re.search(r'helper spread=(\d+) accuracy=(\d+)', text)
                    assert helper_match, 'missing helper summary'
                    result['helpers'] = dict(zip(('spread', 'accuracy'), map(int, helper_match.groups())))
            if run.returncode:
                raise RuntimeError(name + ' failed: ' + str(run.returncode))
        result['product'] = identity(exe)
        result['damageConfigChecks'] = damage_checks
        result['success'] = True
    except BaseException as error:
        result['error'] = str(error)
    finally:
        changed = [row['path'] for row in inputs if identity(row['path']) != row]
        result['changedInputs'] = changed
        if changed:
            result['success'] = False
        write(output / 'result.json', result)
        print(json.dumps({key: value for key, value in result.items() if key != 'actualIncludes'}))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
