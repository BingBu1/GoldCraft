#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run both exact native AddAccount variants with the actual hook registry."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from csol_account_fixture import account_source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('pure', Path(__file__).with_name('run.py'))
    pure = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pure)
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace output directory')
    fixture = Path(__file__).with_name('account.cpp')
    source, inputs = account_source()
    inputs += [fixture, Path(__file__), Path(__file__).with_name('run.py'), args.compiler.resolve(),
               args.compiler.with_name('lld-link.exe').resolve(), args.environment.resolve()]
    before = [pure.identity(path) for path in inputs]
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    out.mkdir(parents=True)
    receipt = dict(success=False, inputsUnchanged=False, inputs=before, steps=[], sourceCopies=0,
                   boundary='Unchanged native account/registry/guard functions; controlled SDK ownership/message edges',
                   gameplay=False, deployed=False)
    def save():
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    try:
        for add in (True, False):
            name = 'add' if add else 'base'
            generated = out / (name + '.cpp')
            generated.write_text(account_source(add)[0] + fixture.read_text('utf8'), 'utf8')
            executable = out / (name + '.exe')
            command = [str(args.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc',
                       '/std:c++20', '/EHsc', '/MT', '/arch:AVX2', '/fp:precise', '/clang:-O3',
                       '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/I' + str(ROOT),
                       '/I' + str(ROOT / 'external/ReGameDLL_CS/regamedll/public/regamedll'),
                       str(generated), '/Fe:' + str(executable), '/link', '/machine:x86', '/threads:1', '/opt:lldltojobs=1']
            build = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=180)
            log = (build.stdout + build.stderr).decode('utf8', errors='replace')
            (out / (name + '-build.log')).write_text(log, 'utf8')
            receipt['steps'].append(dict(name=name + '-build', exit=build.returncode, command=command))
            save()
            if build.returncode:
                raise RuntimeError(log[-6000:])
            run = subprocess.run([str(executable)], cwd=out, env=env, capture_output=True, timeout=30)
            text = (run.stdout + run.stderr).decode('utf8', errors='replace')
            (out / (name + '-tests.log')).write_text(text, 'utf8')
            result = re.fullmatch(r'native damage refund: ([1-9][0-9]*) checks passed\s*', text)
            receipt['steps'].append(dict(name=name + '-tests', exit=run.returncode, checks=int(result[1]) if result else None))
            save()
            if run.returncode or not result:
                raise RuntimeError(text)
        receipt['success'] = True
    finally:
        receipt['changedInputs'] = [row['path'] for row in before if pure.identity(Path(row['path'])) != row]
        receipt['inputsUnchanged'] = not receipt['changedInputs']
        receipt['success'] &= receipt['inputsUnchanged']
        save()
    print(json.dumps(dict(success=receipt['success'], inputsUnchanged=receipt['inputsUnchanged'],
                         checks=sum(row.get('checks', 0) or 0 for row in receipt['steps']))))


if __name__ == '__main__':
    main()
