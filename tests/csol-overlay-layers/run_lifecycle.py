#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run focused overlay/HideWeapon lifecycle regressions against real SDK code.

The old input directory must contain the reviewed pre-fix overlay_layers.cpp,
overlay_layers.hpp and weapons.cpp. The runner never changes production files.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def record(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proposal', 'output', 'old-input'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a fresh workspace build output')
    out.mkdir(parents=True)
    plan = read(args.proposal)
    env = os.environ | read(plan['environment']['path'])
    command = next(j['command'] for j in plan['jobs'] if j['name'] == 'compile-client')
    flags, skip = [], False
    for value in command[1:]:
        if value == '--':
            break
        if skip:
            skip = False
            continue
        if value == '/clang:-ivfsoverlay':
            skip = True
            continue
        if value == '-c' or value.startswith(('/Fo', '/Fd')) or ('\\build\\' in value and value.startswith('-I')):
            continue
        flags.append(value)
    linker = next(j['command'] for j in plan['jobs'] if j['name'] == 'link-client')
    link_flags = [x for x in linker[1:] if not x.startswith(('/out:', '/pdb:')) and not x.endswith('.obj')]
    extractor = ROOT / 'tests/common/cpp_function.py'
    spec = importlib.util.spec_from_file_location('cpp_function', extractor)
    cpp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cpp)
    ext = 'external/MetaHookSv-20261007/Plugins/VGUI2Extension/src/VGUI2ExtensionInternal.cpp'
    roots = ['tests/csol-overlay-layers/lifecycle.cpp', 'tests/csol-overlay-layers/death.cpp',
             'native/csol/client/weapons.cpp', ext]
    frozen, pending = {}, [ROOT / name for name in roots]
    while pending:
        path = pending.pop().resolve()
        if path in frozen:
            continue
        if not path.is_relative_to(ROOT):
            raise ValueError('Source outside workspace')
        data = path.read_bytes()
        target = out / 'source' / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        assert target.stat().st_nlink == 1
        frozen[path] = dict(original=record(path), frozen=record(target))
        for name in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"', data, re.M):
            name = name.decode()
            found = next((p for p in [path.parent / name, ROOT / name] if p.is_file()), None)
            if found:
                pending.append(found)
    overlay = dict(version=0, **{'case-sensitive': False, 'use-external-names': False}, roots=[
        dict(type='file', name=str(p), **{'external-contents': row['frozen']['path']}) for p, row in frozen.items()])
    extension = Path(frozen[ROOT / ext]['frozen']['path']).read_bytes()
    extension = extension[extension.index(b'class CVGUI2Extension '):extension.index(b'static CVGUI2Extension s_VGUI2Extension;')]
    sources, jobs, results, success = {}, [], [], False

    def execute(name, argv, expected=0):
        started = time.monotonic()
        proc = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        log = out / (name + '.log')
        log.write_bytes(proc.stdout)
        row = dict(name=name, command=argv, exitCode=proc.returncode, expectedExit=expected,
                   seconds=time.monotonic()-started, log=record(log))
        jobs.append(row)
        (out / 'jobs.json').write_text(json.dumps(jobs, indent=2), encoding='utf8')
        print(name, proc.returncode, flush=True)
        if proc.returncode != expected:
            print(proc.stdout.decode('utf8', 'replace')[-6000:])
            raise RuntimeError(name + ' failed')
        return proc.stdout

    try:
        for kind in ('lifecycle', 'death'):
            for label, expected in [('working', 0), ('old', 1)]:
                name = kind + '-' + label
                folder = out / name
                generated = folder / 'generated'
                generated.mkdir(parents=True)
                (generated / 'extension.inc').write_bytes(extension)
                ov = json.loads(json.dumps(overlay))
                selected = {}
                for filename in ('overlay_layers.cpp', 'overlay_layers.hpp', 'weapons.cpp'):
                    logical = ROOT / 'native/csol/client' / filename
                    source = args.old_input / filename if label == 'old' else Path(frozen[logical]['frozen']['path'])
                    data = source.read_bytes()
                    target = folder / filename
                    target.write_bytes(data)
                    sources[name + '/' + filename] = dict(original=record(source), tested=record(target))
                    selected[filename] = data
                    next(row for row in ov['roots'] if Path(row['name']) == logical)['external-contents'] = str(target)
                raw = selected['weapons.cpp']
                globals_data = raw[raw.index(b'constexpr int KnifeId'):raw.index(b'bool Custom()')]
                (generated / 'globals.inc').write_bytes(globals_data)
                signatures = [b'void ApplyNativeWeaponVisibility(', b'int Hide(', b'void ResetWeapons(',
                              b'void ClearNativeWeaponSelection(', b'int Reset(']
                extracted = [cpp.function(raw, signature)[2] for signature in signatures]
                redraw = cpp.function(raw, b'int WeaponRedraw(')[2]
                prefix = redraw[redraw.index(b'{')+1:redraw.index(b'    BeginHudStyle(')]
                extracted.append(b'void RedrawDeathPhase()\n{' + prefix + b'\n}\n')
                (generated / 'death.inc').write_bytes(b'\n'.join(extracted))
                overlay_path = folder / 'overlay.json'
                overlay_path.write_text(json.dumps(ov), encoding='utf8')
                obj, exe = folder / 'fixture.obj', folder / 'fixture.exe'
                execute('compile-' + name, [command[0], *flags, '-I' + str(ROOT), '-I' + str(folder),
                    '/clang:-ivfsoverlay', '/clang:' + str(overlay_path), '/Fo' + str(obj),
                    '/Fd' + str(folder / 'fixture.compile.pdb'), '-c', '--',
                    str(ROOT / 'tests/csol-overlay-layers' / (kind + '.cpp'))])
                execute('link-' + name, [linker[0], *link_flags, '/out:' + str(exe),
                                        '/pdb:' + str(folder / 'fixture.pdb'), str(obj)])
                output = execute('run-' + name, [str(exe)], expected)
                counts = re.search(rb'(\d+) checks, (\d+) failures', output)
                if not counts:
                    raise RuntimeError('Missing fixture result')
                if expected:
                    message = (b'explicit shutdown after SDK callback unregisters retained compositor' if kind == 'lifecycle'
                               else b'ResetWeapons clears stale death owner before releasing native mask')
                    if message not in output:
                        raise RuntimeError('Old variant did not fail the intended regression')
                results.append(dict(name=name, checks=int(counts[1]), failures=int(counts[2]), expectedExit=expected))
        success = True
    finally:
        unchanged = all(record(row['original']['path']) == row['original'] for row in frozen.values())
        result = dict(success=success and unchanged, inputsUnchanged=unchanged, results=results,
                      source=list(frozen.values()), variants=sources, jobs=jobs,
                      boundary='Full compositor and exact original CVGUI2Extension class with actual SDK types; factory and painter fixtures. Exact production Hide/Apply/Reset and WeaponRedraw death prefix; later render path/reset modules are controlled boundaries. No game/UI/pixel evidence.')
        (out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    if not unchanged:
        raise RuntimeError('Input source changed during test')


if __name__ == '__main__':
    main()
