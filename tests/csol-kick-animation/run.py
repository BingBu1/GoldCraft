#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the real client phase adapter with exact client SDK/compiler flags."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = path.resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def split(command):
    shell = ctypes.WinDLL('shell32')
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    result = shell.CommandLineToArgvW(command, ctypes.byref(count))
    if not result:
        raise ctypes.WinError()
    try:
        return [result[i] for i in range(count.value)]
    finally:
        free = ctypes.WinDLL('kernel32').LocalFree
        free.argtypes = [ctypes.c_void_p]
        free(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client-build', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', choices=('host', 'predictor'), default='host')
    args = parser.parse_args()
    out, build = args.output.resolve(), args.client_build.resolve(strict=True)
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not build.is_relative_to(ROOT / 'build'):
        raise ValueError('Use existing compiler output and a fresh workspace receipt directory')
    cache = (build / 'CMakeCache.txt').read_text('utf-8-sig')
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache, re.M)[1].strip()
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    database = json.loads(subprocess.check_output([ninja, '-C', str(build), '-t', 'compdb'], env=env))
    row = next(row for row in database if row['file'].replace('\\', '/').endswith('/human_kick_animation.cpp'))
    command = split(row['command'])
    if not command[0].lower().endswith('clang-cl.exe'):
        raise ValueError('Expected the actual configured Clang client compile command')
    flags, skip = [], False
    for argument in command[1:]:
        if skip:
            skip = False
            continue
        if argument.lower() in ('-o', '-mf', '-mt'):
            skip = True
            continue
        if argument.lower() in ('/c', '-c', '-md', '--') or argument.lower().endswith('.cpp') or argument.lower().startswith(('/fo', '/fd', '/fe')):
            continue
        flags.append(argument)
    source = ROOT / 'native/csol/client/scenario/core/human_kick_animation.cpp'
    fixture = ROOT / ('tests/csol-kick-animation/' + args.fixture + '.cpp')
    controls = [source, fixture, ROOT / 'native/csol/client/scenario/core/human_kick_animation_state.hpp',
                ROOT / 'native/csol/client/scenario/core/human_kick.hpp', ROOT / 'native/csol/scenario/core/human_kick.hpp',
                Path(__file__).resolve(), args.environment.resolve(), build / 'build.ninja', build / 'CMakeCache.txt', Path(command[0])]
    before = [identity(path) for path in controls]
    out.mkdir(parents=True)
    steps, includes, objects = [], {}, []
    for name, unit in (('adapter', source), ('host', fixture)):
        obj = out / (name + '.obj')
        objects.append(obj)
        job = [command[0], *flags, '/c', '/showIncludes', '/Fo' + str(obj), str(unit)]
        done = subprocess.run(job, cwd=build, env=env, capture_output=True, timeout=180)
        text = (done.stdout + done.stderr).decode('utf8', errors='replace')
        (out / (name + '-build.log')).write_text(text, 'utf8')
        steps.append(dict(name=name, command=job, exit=done.returncode))
        for path in re.findall(r'Note: including file:\s*([^\r\n]+)', text):
            path = Path(path.strip()).resolve(strict=True)
            includes[str(path)] = identity(path)
        if done.returncode:
            print(text[-7000:])
            raise SystemExit(1)
    executable = out / 'animation.exe'
    job = [command[0], '--target=i686-pc-windows-msvc', '/MT', '/clang:-fuse-ld=lld', *map(str, objects),
           '/Fe:' + str(executable), '/link', '/machine:x86', '/subsystem:console', '/threads:1', '/opt:lldltojobs=1']
    linked = subprocess.run(job, cwd=out, env=env, capture_output=True, timeout=180)
    text = (linked.stdout + linked.stderr).decode('utf8', errors='replace')
    (out / 'link.log').write_text(text, 'utf8')
    steps.append(dict(name='link', command=job, exit=linked.returncode))
    run = subprocess.run([str(executable)], cwd=out, env=env, capture_output=True, timeout=30) if not linked.returncode else None
    text = (run.stdout + run.stderr).decode('utf8', errors='replace') if run else text
    (out / 'test.log').write_text(text, 'utf8')
    summary = re.search(r'^kick animation (?:host|predictor): ([1-9][0-9]*) checks(?: passed|, 0 failures)\s*$', text, re.M)
    stable = all(identity(Path(row['path'])) == row for row in before + list(includes.values()))
    receipt = dict(success=not linked.returncode and run is not None and not run.returncode and bool(summary) and stable,
                   inputsUnchanged=stable, checks=int(summary[1]) if summary else None, steps=steps, inputs=before,
                   includes=list(includes.values()), sourceCopies=0, deployed=False, gameplay=False,
                   boundary='Actual client phase adapter over SDK engine globals, message/model/prediction edges')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps({key: receipt[key] for key in ('success', 'checks', 'inputsUnchanged')}))
    if not receipt['success']:
        print(text)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
