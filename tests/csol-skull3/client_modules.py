#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and test every module against the exact completed client DLL sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def ident(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def key(path):
    return os.path.normcase(str(Path(path).resolve()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--native-spans', type=Path, required=True)
    parser.add_argument('--reuse', type=Path)
    args = parser.parse_args()
    baseline, out = args.baseline.resolve(), args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    assert read(baseline / 'execution-handoff.json')['success']
    out.mkdir()
    source = out / 'source'
    (out / 'temp').mkdir()
    manifest = {row['relative']: row['frozen'] for row in
                read(baseline / 'effective-source-manifest.json')['files']}
    known = {key(row['path']): row for row in read(baseline / 'inputs.json')['files']}
    frozen = {}

    def freeze(name):
        if name in frozen:
            return
        original = ident(ROOT / name) if name.startswith('tests/') else manifest[name]
        assert ident(original['path']) == original
        data = Path(original['path']).read_bytes()
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        assert target.stat().st_nlink == 1
        frozen[name] = dict(relative=name, original=original, frozen=ident(target))
        known[key(target)] = ident(target)
        for token in re.findall(r'^\s*#include\s+"([^"]+)"', data.decode('utf-8-sig'), re.M):
            child = Path(os.path.normpath(str(Path(name).parent / token))).as_posix()
            if child in manifest or (ROOT / child).is_file():
                freeze(child)
            elif token in manifest:
                freeze(token)

    roots = ['native/csol/client/' + name + '.cpp' for name in (
        'session', 'weapon_inventory', 'weapons', 'crosshair', 'pickup_history',
        'beamgun', 'cannon', 'k3', 'melee', 'm249ex', 'balrog11', 'death_notices',
        'skull1', 'skull2', 'skull3', 'skull5', 'shared_random', 'weapon_module',
        'gun_smoke')]
    roots += ['native/csol/' + name + '.cpp' for name in
              ('beamgun_state', 'weapon_slots', 'melee_attacks')]
    roots += ['tests/csol-weapon-slots/session_edges.cpp',
              'tests/csol-skull3/client_modules.cpp']
    for name in roots:
        freeze(name)
    generated = out / 'generated'
    generated.mkdir()
    (generated / 'native_spans.inc').write_bytes(args.native_spans.read_bytes())
    for path in (args.native, args.native_spans, generated / 'native_spans.inc', Path(__file__)):
        known[key(path)] = ident(path)
    native_identity = ident(args.native)
    assert native_identity['sha256'] == 'b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2'
    command = next(row['command'] for row in read(baseline / 'build-commands.json')
                   if row['kind'] == 'compile' and row['command'][-1].endswith('skull3.cpp'))
    base, skip = [], False
    for arg in command:
        if arg == '--':
            break
        if skip:
            skip = False
            continue
        if arg == '/clang:-ivfsoverlay':
            skip = True
            continue
        if arg.startswith(('/Fo', '/Fd')):
            continue
        base.append(arg)
    base += ['-I' + str(source), '-I' + str(generated)]
    environment = read(baseline / 'environment.json') | dict(TEMP=str(out / 'temp'), TMP=str(out / 'temp'))
    commands, objects, reused = [], [], []
    if args.reuse:
        old_plan = read(args.reuse / 'plan.json')
        old_result = read(args.reuse / 'result.json')
        old_sources = {row['relative']: row for row in old_plan['sources']}
        for name, row in frozen.items():
            if name != 'tests/csol-skull3/client_modules.cpp':
                assert row['frozen']['sha256'] == old_sources[name]['frozen']['sha256'], name
                assert ident(old_sources[name]['frozen']['path']) == old_sources[name]['frozen']
    for name in roots:
        stem = Path(name).stem
        obj = out / (stem + '.obj')
        old_step = next((row for row in old_result['steps'] if row['name'] == stem and row['exitCode'] == 0), None) if args.reuse else None
        old_reused = next((row for row in old_plan['reused'] if Path(row['path']).stem == stem), None) if args.reuse else None
        if args.reuse and stem != 'client_modules' and (old_step or old_reused):
            obj = (args.reuse / (stem + '.obj')).resolve()
            if not obj.exists():
                obj = Path(next(row['path'] for row in old_plan['reused'] if Path(row['path']).stem == stem))
            reused.append(ident(obj))
        else:
            extra = ['/clang:-finstrument-functions'] if name.startswith('native/') else []
            commands.append(dict(name=stem, kind='compile', argv=[*base, *extra,
                '/Fo' + str(obj), '/Fd' + str(out / (stem + '.pdb')), '--', str(source / name)]))
        objects.append(obj)
    exe = out / 'client_modules.exe'
    commands.append(dict(name='link', kind='link', argv=[base[0], '--target=i686-pc-windows-msvc',
        '-fuse-ld=lld', '/MT', *map(str, objects), '/Fe' + str(exe), '/link', '/OPT:REF',
        '/OPT:LLDLTO=3', '/threads:1', '/opt:lldltojobs=1']))
    commands.append(dict(name='test', kind='test', argv=[str(exe), str(args.native.resolve()),
                                                       str(out / 'observations.txt')]))
    plan = dict(jobs=1, sources=list(frozen.values()), commands=commands, reused=reused,
                baseline=ident(baseline / 'execution-handoff.json'), native=native_identity,
                inputs=list(known.values()), environment=environment,
                boundary='Real weapon/controller/slot TUs. Controlled SDK and UI/GL boundaries. No game or GPU acceptance.')
    (out / 'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
    started, steps, unknown, actual = time.monotonic(), [], [], {}
    error = None
    try:
        for row in commands:
            print('START', row['name'], flush=True)
            run = subprocess.run(row['argv'], cwd=out, env=os.environ | environment,
                                 capture_output=True, timeout=180)
            log = out / (row['name'] + '.log')
            log.write_bytes(run.stdout + run.stderr)
            content = log.read_text('utf-8', errors='replace')
            step = dict(name=row['name'], exitCode=run.returncode, log=ident(log))
            steps.append(step)
            for path in re.findall(r'^\s*Note: including file:\s*(.+?)\s*$', content, re.M):
                token = key(path)
                if token not in known:
                    unknown.append(path)
                else:
                    assert ident(path) == known[token], path
                    actual[token] = known[token]
            if row['kind'] == 'test':
                match = re.search(r'checks=(\d+) failures=(\d+)', content)
                if match:
                    step.update(checks=int(match[1]), failures=int(match[2]))
                else:
                    error = 'No completed behavioral summary'
            print(row['name'], run.returncode, flush=True)
            if run.returncode or unknown or error:
                print(content[-12000:], flush=True)
                break
    except BaseException as exc:
        error = str(exc)
    unchanged = all(ident(row['frozen']['path']) == row['frozen'] for row in frozen.values())
    unchanged &= all(ident(row['path']) == row for row in reused)
    unchanged &= all(ident(row['path']) == row for row in actual.values())
    unchanged &= ident(args.native) == native_identity
    result = dict(success=len(steps) == len(commands) and all(row['exitCode'] == 0 for row in steps)
                  and not unknown and not error and unchanged, steps=steps, error=error,
                  actualInputs=list(actual.values()), unknownInputs=unknown, inputsUnchanged=unchanged,
                  seconds=time.monotonic()-started, plan=ident(out / 'plan.json'), runner=ident(__file__),
                  liveGameplay=False)
    (out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'actualInputs'}), flush=True)
    assert result['success']


if __name__ == '__main__':
    main()
