#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run complete BOSS HUD resource precaching against verified GameDLL objects."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HEADER = 'native/csol/boss_health.hpp'
HOST = 'native/csol/server/scenario_session.cpp'
FIXTURE = 'tests/csol-boss-resource-precache/resources_sdk.cpp'


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def record(path):
    path = Path(path).resolve()
    raw = path.read_bytes()
    return dict(path=str(path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def key(path):
    return os.path.normcase(str(Path(path).resolve()))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'edges', 'server', 'common', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    assert args.server.resolve(strict=True).is_dir(), 'Actual cstrike directory required'
    assert args.common.resolve(strict=True).is_dir(), 'Extracted common directory required'
    baseline, out = args.baseline.resolve(), args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir(parents=True)
    selected = {row['relative']: Path(row['frozen']['path'])
                for row in read(baseline / 'effective-source-manifest.json')['files']}
    known = {key(row['path']): row for row in read(baseline / 'inputs.json')['files']}
    original = {rel: record(selected[rel]) for rel in (HEADER, HOST)}
    for rel in (HEADER, HOST):
        source = ROOT / rel
        destination = out / 'source' / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        assert destination.stat().st_nlink == 1 and destination.stat().st_ino != source.stat().st_ino
        selected[rel] = destination
    old_text = Path(original[HOST]['path']).read_text('utf8')
    candidate_text = selected[HOST].read_text('utf8')
    removed = candidate_text
    for line in ('#include \"../boss_health.hpp\"\n',
                 '    for (const auto* image : csol::boss_health::Resources) hudPaths.emplace(image);\n',
                 '    for (const auto* image : csol::boss_health::AbilityResources) hudPaths.emplace(image);\n'):
        assert removed.count(line) == 1, 'Expected one precise BOSS registration delta'
        removed = removed.replace(line, '')
    assert removed == old_text, 'Candidate contains unrelated Session changes'
    # Freeze test includes; production headers remain on the exact baseline.
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
    overlay = {key(row['name']): row for row in read(baseline / 'source-overlay.json')['roots']}
    for relative, source in selected.items():
        for alias in (ROOT / relative, out / 'source' / relative, source):
            overlay[key(alias)] = dict(type='file', name=str(alias), **{'external-contents': str(source)})
    for row in overlay.values():
        for relative in (HEADER, HOST):
            if row['name'].replace('\\', '/').lower().endswith('/' + relative):
                row['external-contents'] = str(selected[relative])
    write(out / 'overlay.json', dict(version=0, **{'case-sensitive': False, 'use-external-names': True},
                                     roots=list(overlay.values())))
    for path in [*selected.values(), out / 'overlay.json', Path(__file__)]:
        known[key(path)] = record(path)
    assets = re.findall(r'"(resource/[^"]+\.tga)"', selected[HEADER].read_text('utf8'))
    assert len(assets) == 8 and len(set(assets)) == 8
    asset_manifest = []
    for relative in assets:
        source, destination = args.common.resolve() / relative, out / 'resources' / relative
        before = record(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        copied = record(destination)
        assert before['sha256'] == copied['sha256'] and copied['bytes'] > 0
        assert destination.stat().st_nlink == 1
        assert source.stat().st_ino != destination.stat().st_ino
        raw = destination.read_bytes()
        asset_manifest.append(dict(relative=relative, original=before, frozen=copied,
                                   dimensions=[int.from_bytes(raw[12:14], 'little'), int.from_bytes(raw[14:16], 'little')]))
        known[key(source)], known[key(destination)] = before, copied
    (out / 'resources' / 'empty-file').write_bytes(b'')
    known[key(out / 'resources' / 'empty-file')] = record(out / 'resources' / 'empty-file')
    write(out / 'resources.json', asset_manifest)
    commands = read(baseline / 'build-commands.json')
    template = next(job['command'] for job in commands if job['kind'] == 'compile')

    def compile_command(relative, name):
        command = list(template)
        for i, arg in enumerate(command):
            if arg.lower().endswith('.cpp'):
                command[i] = str(selected[relative])
            elif arg.startswith('/Fo'):
                command[i] = '/Fo' + str(out / (name + '.obj'))
            elif arg.startswith('/Fd'):
                command[i] = '/Fd' + str(out / (name + '.compile.pdb'))
            elif arg.startswith('/clang:') and arg.endswith('source-overlay.json'):
                command[i] = '/clang:' + str(out / 'overlay.json')
        return command

    units = read(baseline / 'complete-object-provenance.json')['units']
    host_object = next(unit['object'] for unit in units
                       if unit['source']['path'].replace('\\', '/').lower().endswith('/' + HOST))
    assert record(host_object['path']) == host_object
    jobs = [dict(name='fixture', command=compile_command(FIXTURE, 'fixture')),
            dict(name='host', command=compile_command(HOST, 'host'))]
    for variant in ('old', 'current'):
        command = []
        for arg in commands[-1]['command']:
            if arg.lower() == '/dll' or arg.lower().startswith(('/out:', '/pdb:', '/implib:', '/def:',
                                                               '/ltcgout:', '/manifest', '/natvis:')):
                continue
            if variant == 'current' and key(arg) == key(host_object['path']):
                arg = str(out / 'host.obj')
            elif Path(arg).is_absolute() and Path(arg).suffix.lower() in ('.obj', '.lib', '.res'):
                actual = record(arg)
                if key(arg) in known:
                    assert actual == known[key(arg)]
                known[key(arg)] = actual
            command.append(arg)
        command += ['/OUT:' + str(out / (variant + '.exe')), '/PDB:' + str(out / (variant + '.pdb')),
                    '/SUBSYSTEM:CONSOLE', '/threads:1', '/opt:lldltojobs=1',
                    '/lldltocache:' + str(out / 'lto-cache'), str(out / 'fixture.obj')]
        jobs.append(dict(name='link-' + variant, command=command))
    environment = os.environ | read(baseline / 'environment.json')
    environment.update(TEMP=str(out), TMP=str(out), VSLANG='1033')
    # The new fixture uses the standard filesystem/stream headers. Capture
    # their installed toolchain tree before compiling, then audit actual includes.
    for directory in environment['INCLUDE'].split(';'):
        directory = Path(directory)
        if (directory / 'filesystem').is_file():
            for path in directory.iterdir():
                if path.is_file():
                    known[key(path)] = record(path)
    result = dict(success=False, baseline=str(baseline), original=original,
                  candidate={rel: record(selected[rel]) for rel in (HEADER, HOST)},
                  jobs=[], runs=[], boundary='Actual complete Session::Precache TU and matched GameDLL SDK objects. '
                  'Controlled engine boundaries and actual disk resources. Verifies 8 BOSS HUD images, '
                  'missing/empty failure, Selection/Combat, reentry and map retirement. '
                  'Not live HTTP download, deployment or graphical BOSS rendering evidence.')
    write(out / 'plan.json', dict(jobs=jobs, inputs=list(known.values()), candidate=result['candidate'],
                                 original=original))

    def save():
        write(out / 'result.json', result)

    try:
        for job in jobs:
            response = out / (job['name'] + '.rsp')
            response.write_text(subprocess.list2cmdline(job['command'][1:]) + '\n', encoding='utf8')
            start = time.monotonic()
            process = subprocess.run([job['command'][0], '@' + str(response)], cwd=out, env=environment,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
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
            result['jobs'].append(row)
            save()
            assert not process.returncode and not row.get('unknown') and not row.get('changed'), job['name'] + ' failed'
        for variant, mode in [('old', -1)] + [('current', i) for i in range(-1, 22)]:
            process = subprocess.run([str(out / (variant + '.exe')), str(mode), str(args.server.resolve()),
                                      str(out / 'resources')], cwd=out, env=environment,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            log = out / (variant + '-run-' + str(mode) + '.log')
            log.write_bytes(process.stdout)
            text = process.stdout.decode('utf8', errors='replace')
            for line in text.splitlines():
                if line.startswith('FILE\t'):
                    _, size, relative, path = line.split('\t', 3)
                    actual = record(path)
                    assert actual['bytes'] == int(size), 'Observed resource size changed'
                    if key(path) in known:
                        assert actual == known[key(path)], 'Observed backing resource changed'
                    known[key(path)] = actual
            expected = 1 if variant == 'old' else 0
            failure_lines = [line for line in text.splitlines() if line.startswith('ASSERT ') or line.startswith('EXCEPTION ')]
            observed = process.returncode == expected
            if variant == 'old':
                observed &= failure_lines.count('ASSERT Combat advertises each BOSS HUD asset exactly once') == 8
                observed &= len(failure_lines) == 8
            else:
                observed &= 'failures=0' in text and not failure_lines
            result['runs'].append(dict(variant=variant, mode=mode, exit=process.returncode,
                                       expectedExit=expected, expectedObserved=observed, log=record(log),
                                       summary=text.splitlines()[-1:])); save()
            assert observed, variant + '/' + str(mode) + ' outcome differs'
        write(out / 'verified-inputs.json', dict(files=list(known.values())))
        result['changedInputs'] = [path for path, row in known.items() if record(path) != row]
        result['success'] = not result['changedInputs']
        save()
    except BaseException as error:
        result['error'] = str(error)
        save()
    print(json.dumps({key: value for key, value in result.items() if key not in ('jobs', 'runs')}, ensure_ascii=False))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
