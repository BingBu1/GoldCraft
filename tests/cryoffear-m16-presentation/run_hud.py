# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual HUD parser and complete cold-loader/draw fixture in place."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('native/csol/hud_style.cpp', 'native/csol/image_codec.cpp',
           'tests/csol-hud-style/pure.cpp', 'tests/csol-hud-style/client.cpp',
           'native/csol/client/resource_font_memory.cpp')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('environment', 'commands', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (out, *out.parents)):
        raise ValueError('Fresh workspace build output required')
    out.mkdir(parents=True)
    config = ROOT/'.tools/clang-toolchain.json'
    compiler = Path(json.loads(config.read_text('utf-8-sig'))['root'])/'bin/clang-cl.exe'
    database = json.loads(args.commands.read_text('utf8'))
    command = next(row['command'] for row in database if row['file'].replace(chr(92), '/').endswith('/native/csol/client/hud_style.cpp'))
    tokens = re.findall(r'"[^"\r\n]*"|[^\s]+', command)
    flags = ['/nologo', '/TP', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/W3', '/WX',
             '--target=i686-pc-windows-msvc', '/O2', '/clang:-O3', '/clang:-flto=thin',
             '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off', '-Wno-overriding-option',
             '/DNDEBUG', '/I'+str(ROOT), *[s for s in tokens if s.startswith('-D')],
             *['/imsvc'+s[2:] for s in tokens if s.startswith('-I')]]
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    inputs = {ROOT/name for name in SOURCES} | {Path(__file__), config, args.commands, args.environment,
                                              compiler, compiler.with_name('lld-link.exe')}
    steps = []

    def run(name, argv):
        done = subprocess.run(list(map(str, argv)), cwd=ROOT, env=env, capture_output=True,
                              text=True, encoding='utf8', errors='replace', timeout=150)
        (out/(name+'.log')).write_text(done.stdout+done.stderr, 'utf8')
        steps.append(dict(name=name, command=list(map(str, argv)), exit=done.returncode))
        (out/'steps.json').write_text(json.dumps(steps, indent=2), 'utf8')
        if done.returncode:
            print(done.stdout+done.stderr, flush=True)
            done.check_returncode()
        return done.stdout+done.stderr

    for index, name in enumerate(SOURCES):
        dep = out/f'{index}.d'
        run(f'deps-{index}', [compiler, *flags, '/clang:-M', '/clang:-MF', '/clang:'+str(dep), ROOT/name])
        make = dep.read_text().replace(chr(92)+chr(10), ' ')
        inputs.update(Path(p.replace(chr(92)+' ', ' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(p):sha(p) for p in sorted(inputs)}
    objects = []
    for index, name in enumerate(SOURCES):
        obj = out/f'{index}.obj'
        extra = ['/clang:-Wno-macro-redefined'] if name.endswith('/resource_font_memory.cpp') else []
        run(f'compile-{index}', [compiler, *flags, *extra, '/c', '/Fo'+str(obj), ROOT/name])
        objects.append(obj)
    results = []
    for name, indices in (('pure', (0, 2)), ('client', (0, 1, 3, 4))):
        exe = out/(name+'.exe')
        run('link-'+name, [compiler.with_name('lld-link.exe'), '/nologo', '/machine:x86', '/subsystem:console',
                          '/threads:1', '/opt:ref', '/out:'+str(exe), *[objects[i] for i in indices],
                          'gdi32.lib', 'user32.lib', 'ole32.lib'])
        results.append(run('test-'+name, [exe]))
    after = {str(p):sha(p) for p in sorted(inputs)}
    receipt = dict(success=before==after, before=before, after=after, inputsUnchanged=before==after,
                   steps=steps, results=results, sourceCopies=0, gameplay=False, performance=False,
                   boundary='Actual complete HUD parser/cold loader/draw paths; controlled VGUI/engine/font/resources, no GPU or game acceptance')
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf8')
    print(''.join(results), flush=True)
    if not receipt['success']:
        raise RuntimeError('Inputs changed during test')


if __name__ == '__main__':
    main()
