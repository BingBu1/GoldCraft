#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile G43 aim controls and compare complete original x86 action starts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from oracle import generate

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sample', 'model', 'proof', 'compiler', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    for path in (out, *out.parents):
        if path.is_symlink() or os.path.isjunction(path):
            raise ValueError('Linked output directory')
    out.mkdir(parents=True)
    sources = [Path(__file__).with_name('main.cpp'), ROOT / 'native/guest/cof/g43_aim.cpp']
    dependencies = {*sources, Path(__file__).resolve(), Path(__file__).with_name('oracle.py'),
        ROOT / 'tests/cryoffear-magazine-actions/run.py',
        ROOT / 'tests/cryoffear-tmp-weapon/transition_oracle.py', ROOT / 'tests/cryoffear-tmp-weapon/oracle.py',
        ROOT / 'tests/cryoffear-p345-weapon/oracle.py', ROOT / 'tests/cryoffear-ballistics/oracle.py', args.environment.resolve(),
        args.proof.resolve(), args.sample.resolve(), args.model.resolve()}
    proof = json.loads(args.proof.read_text('utf-8-sig'))
    dependencies.update((args.proof.parent / name).resolve() for name in proof['archives'])
    with (out / 'cases.txt').open('w') as cases, (out / 'expected.txt').open('w') as expected, \
            (out / 'times.txt').open('w') as timing:
        original = generate(args.sample, args.model, args.proof, cases, expected, timing)
    dependencies.update(out / name for name in ('cases.txt', 'expected.txt', 'times.txt'))
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
        '/W4', '/WX', '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2',
        '/fp:precise', '/clang:-ffp-contract=off', '-Wno-overriding-option', '/Gy', '/Gw', '/I' + str(ROOT)]
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    (out / 'temp').mkdir()
    env.update(TEMP=str(out / 'temp'), TMP=str(out / 'temp'))
    commands = []
    for index, source in enumerate(sources):
        dep = out / f'{index}.d'
        command = [str(args.compiler), *flags, '/clang:-M', '/clang:-MF', '/clang:' + str(dep), str(source)]
        commands.append(command)
        result = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=60)
        (out / f'{index}-headers.log').write_text(result.stdout + result.stderr, 'utf-8')
        result.check_returncode()
        make = dep.read_text().replace(chr(92) + chr(10), ' ')
        dependencies.update(Path(value.replace(chr(92) + ' ', ' ')).resolve()
            for value in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(path): sha(path) for path in sorted(dependencies)}
    compiler_hash = sha(args.compiler)
    executable = out / 'g43-aim.exe'
    command = [str(args.compiler), *flags, *map(str, sources), '/Fe:' + str(executable),
        '/link', '/machine:x86', '/threads:1', '/opt:ref']
    compiled = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=120)
    (out / 'compile.log').write_text(compiled.stdout + compiled.stderr, 'utf-8')
    tested, negatives = None, []
    if not compiled.returncode:
        tested = subprocess.run([str(executable), str(out / 'cases.txt'), str(out / 'expected.txt'),
            str(out / 'times.txt')], cwd=out, capture_output=True, text=True, timeout=30)
        (out / 'test.log').write_text(tested.stdout + tested.stderr, 'utf-8')
    if tested is not None and not tested.returncode:
        rows, answers = (out / 'cases.txt').read_text(), (out / 'expected.txt').read_text()
        for name, cases, expected, wanted in (
                ('empty', '', answers, 6), ('truncated', rows.splitlines()[0].rsplit(' ', 1)[0], answers, 3),
                ('wrong-result', rows, 'ffffffff ' + answers.split(' ', 1)[1], 5),
                ('extra-result', rows, answers + '00000000\n', 7)):
            c, e = out / (name + '-cases.txt'), out / (name + '-expected.txt')
            c.write_text(cases); e.write_text(expected)
            result = subprocess.run([str(executable), str(c), str(e), str(out / 'times.txt')],
                cwd=out, capture_output=True, text=True, timeout=30)
            (out / (name + '.log')).write_text(result.stdout + result.stderr, 'utf-8')
            negatives.append(dict(name=name, actual=result.returncode, expected=wanted, passed=result.returncode == wanted))
    after = {str(path): sha(path) for path in sorted(dependencies)}
    success = not compiled.returncode and tested is not None and not tested.returncode and before == after and \
        sha(args.compiler) == compiler_hash and all(item['passed'] for item in negatives)
    (out / 'receipt.json').write_text(json.dumps(dict(success=success, original=original,
        command=command, dependencyCommands=commands, before=before, after=after, negative=negatives,
        compilerSha256=compiler_hash, compiled=compiled.returncode, tested=tested.returncode if tested else None,
        sourceCopies=0, fullDLL=False, deployed=False, boundary=original['boundary']), indent=2) + '\n', 'utf-8')
    print(tested.stdout + tested.stderr if tested else compiled.stdout + compiled.stderr)
    return 0 if success else 1


if __name__ == '__main__':
    raise SystemExit(main())
