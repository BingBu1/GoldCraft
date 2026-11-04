#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the complete melee weapon TU with matched ReGameDLL SDK objects."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HOST = 'native/csol/server/wpn_melee.cpp'
FIXTURE = 'tests/csol-melee-sound/sdk.cpp'
EXPECTED_OLD_FAILURES = {
    'ASSERT original per-target or per-completion sound count': 5,
    'ASSERT original hit/miss table sample selected': 7,
    'ASSERT only original helper paths request random samples': 14,
    'ASSERT original sample-count RNG bounds': 2,
    'ASSERT original result channel': 6,
    'ASSERT original result pitch': 6,
    'ASSERT original sound versus target-damage ordering': 4,
    'ASSERT retiring callback precedes damage on SkullAxe': 5,
    'ASSERT cancelled attack stops at exact sound boundary': 1,
    'ASSERT cancelled sound boundary cannot publish completion or animation': 1,
}


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def record(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def key(path):
    # Keys are lexical aliases; record() resolves and hashes the actual inputs.
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'edges', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--lto-cache', type=Path)
    args = parser.parse_args()
    baseline, out = args.baseline.resolve(), args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir(parents=True)
    cache = args.lto_cache.resolve() if args.lto_cache else out / 'lto-cache'
    assert cache.is_relative_to(ROOT / 'build')
    selected = {row['relative']: Path(row['frozen']['path'])
                for row in read(baseline / 'effective-source-manifest.json')['files']}
    known = {key(row['path']): row for row in read(baseline / 'inputs.json')['files']}
    originals = dict(old=record(selected[HOST]), current=record(ROOT / HOST))
    hosts = {}
    for variant, source in originals.items():
        destination = out / variant / HOST
        destination.parent.mkdir(parents=True)
        shutil.copy2(source['path'], destination)
        assert destination.stat().st_nlink == 1
        assert destination.stat().st_ino != Path(source['path']).stat().st_ino
        text = destination.read_text('utf8')
        assert text.count('private:') == 1, 'Test access must be one exact class access specifier'
        exposed = out / ('access-' + variant) / HOST
        exposed.parent.mkdir(parents=True)
        exposed.write_text(text.replace('private:', 'public:'), encoding='utf8')
        hosts[variant] = destination
        hosts['access-' + variant] = exposed
    pending, captured = [ROOT / FIXTURE], set()
    while pending:
        source = pending.pop().resolve()
        relative = source.relative_to(ROOT).as_posix()
        if relative in captured:
            continue
        captured.add(relative)
        if relative in selected:
            continue
        destination = out / 'source' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        selected[relative] = destination
        for include in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"', source.read_bytes(), re.M):
            path = (source.parent / include.decode()).resolve()
            if path.is_file() and path.is_relative_to(ROOT):
                pending.append(path)
    for name in ('engine_edge_names.inc', 'engine_edge_defaults.inc'):
        relative = 'tests/csol-scenario-rules-session/' + name
        destination = out / 'source' / relative
        shutil.copy2(args.edges / name, destination)
        selected[relative] = destination
    overlays = {}
    for variant, host in hosts.items():
        overlay = {key(row['name']): dict(row) for row in read(baseline / 'source-overlay.json')['roots']}
        chosen = selected | {HOST: host}
        for relative, source in chosen.items():
            for alias in (ROOT / relative, out / 'source' / relative, source, selected[relative]):
                overlay[key(alias)] = dict(type='file', name=str(alias), **{'external-contents': str(source)})
        for row in overlay.values():
            if row['name'].replace('\\', '/').lower().endswith('/' + HOST):
                row['external-contents'] = str(host)
        path = out / (variant + '-overlay.json')
        write(path, dict(version=0, **{'case-sensitive': False, 'use-external-names': True}, roots=list(overlay.values())))
        overlays[variant] = path
    for path in [*selected.values(), *hosts.values(), *overlays.values(), Path(__file__)]:
        known[key(path)] = record(path)
    commands = read(baseline / 'build-commands.json')
    template = next(row['command'] for row in commands if row['kind'] == 'compile')

    def compile_command(source, name, variant):
        command = list(template)
        # Moving the TU into its frozen directory must retain quoted includes
        # from the original local server directory, before SDK weapons.h.
        command.insert(1, '/I' + str(ROOT / 'native/csol/server'))
        for i, arg in enumerate(command):
            if arg.lower().endswith('.cpp'):
                command[i] = str(source)
            elif arg.startswith('/Fo'):
                command[i] = '/Fo' + str(out / (name + '.obj'))
            elif arg.startswith('/Fd'):
                command[i] = '/Fd' + str(out / (name + '.compile.pdb'))
            elif arg.startswith('/clang:') and arg.endswith('source-overlay.json'):
                command[i] = '/clang:' + str(overlays[variant])
        return command

    units = read(baseline / 'complete-object-provenance.json')['units']
    host_object = next(unit['object'] for unit in units
                       if unit['source']['path'].replace('\\', '/').lower().endswith('/' + HOST))
    assert record(host_object['path']) == host_object
    jobs = [dict(name='production', command=compile_command(hosts['current'], 'production', 'current'))]
    for variant in ('current', 'old'):
        name = 'fixture-' + variant
        jobs.append(dict(name=name, command=compile_command(selected[FIXTURE], name, 'access-' + variant)))
        command = []
        for arg in commands[-1]['command']:
            if arg.lower() == '/dll' or arg.lower().startswith(('/out:', '/pdb:', '/implib:', '/def:',
                                                               '/ltcgout:', '/manifest', '/natvis:')):
                continue
            if key(arg) == key(host_object['path']):
                continue
            if Path(arg).is_absolute() and Path(arg).suffix.lower() in ('.obj', '.lib', '.res'):
                actual = record(arg)
                if key(arg) in known:
                    assert actual == known[key(arg)]
                known[key(arg)] = actual
            command.append(arg)
        command += ['/OUT:' + str(out / (variant + '.exe')), '/PDB:' + str(out / (variant + '.pdb')),
                    '/SUBSYSTEM:CONSOLE', '/threads:1', '/opt:lldltojobs=1',
                    '/lldltocache:' + str(cache), str(out / (name + '.obj'))]
        jobs.append(dict(name='link-' + variant, command=command))
    environment = os.environ | read(baseline / 'environment.json')
    environment.update(TEMP=str(out), TMP=str(out), VSLANG='1033')
    for directory in environment['INCLUDE'].split(';'):
        directory = Path(directory)
        if (directory / 'filesystem').is_file():
            for path in directory.iterdir():
                if path.is_file():
                    known[key(path)] = record(path)
    result = dict(success=False, baseline=str(baseline), source=originals, jobs=[], runs=[],
                  boundary='Complete production melee TU and matched GameDLL objects. Test-only class access change; '
                  'no production method substitutions. Controlled engine traces, sound and RNG. Not live gameplay/audio evidence.')
    write(out / 'plan.json', dict(jobs=jobs, inputs=list(known.values()), source=originals))

    def save():
        write(out / 'result.json', result)

    try:
        for job in jobs:
            response = out / (job['name'] + '.rsp')
            response.write_text(subprocess.list2cmdline(job['command'][1:]) + '\n', encoding='utf8')
            start = time.monotonic()
            process = subprocess.run([job['command'][0], '@' + str(response)], cwd=out, env=environment,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
            log = out / (job['name'] + '.log')
            log.write_bytes(process.stdout)
            row = dict(name=job['name'], exit=process.returncode, seconds=time.monotonic() - start,
                       log=record(log), response=record(response))
            if not job['name'].startswith('link'):
                included = set(key(path.strip()) for path in re.findall(
                    r'Note: including file:\s*(.+)', process.stdout.decode(errors='replace')))
                row['unknown'] = [path for path in included if path not in known]
                row['changed'] = [path for path in included if path in known and record(path) != known[path]]
                row['actualIncludes'] = [record(path) for path in sorted(included)]
            result['jobs'].append(row); save()
            assert not process.returncode and not row.get('unknown') and not row.get('changed'), job['name'] + ' failed'
        for variant in ('current', 'old'):
            process = subprocess.run([str(out / (variant + '.exe'))], cwd=out, env=environment,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            log = out / (variant + '-run.log'); log.write_bytes(process.stdout)
            text = process.stdout.decode('utf8', errors='replace')
            expected = 0 if variant == 'current' else 1
            failure_lines = [line for line in text.splitlines() if line.startswith(('ASSERT ', 'EXCEPTION ', 'FAIL '))]
            observed = process.returncode == expected and ('SDK failures=0' in text)
            if variant == 'current':
                observed &= 'failures=0' in text and not failure_lines
            else:
                observed &= Counter(failure_lines) == EXPECTED_OLD_FAILURES
            result['runs'].append(dict(variant=variant, exit=process.returncode, expectedExit=expected,
                                       expectedObserved=observed, failures=failure_lines, log=record(log), summary=text.splitlines()[-1:])); save()
            assert observed, variant + ' outcome differs'
        write(out / 'verified-inputs.json', dict(files=list(known.values())))
        result['changedInputs'] = [path for path, row in known.items() if record(path) != row]
        result['success'] = not result['changedInputs']; save()
    except BaseException as error:
        result['error'] = str(error); save()
    print(json.dumps({k: v for k, v in result.items() if k not in ('jobs', 'runs')}, ensure_ascii=False))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
