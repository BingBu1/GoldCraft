#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile FAMAS primary and compare full original x86 instructions."""
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
    for name in ('sample', 'proof', 'compiler', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    for path in (output, *output.parents):
        if path.is_symlink() or os.path.isjunction(path):
            raise ValueError('Linked output path')
    if output.exists() or not output.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    sources = [Path(__file__).with_name('main.cpp'), ROOT / 'native/guest/cof/famas_shot.cpp']
    dependencies = set(sources) | {Path(__file__).resolve(), Path(__file__).with_name('oracle.py'),
        ROOT / 'tests/cryoffear-mp5-primary/oracle.py', ROOT / 'tests/cryoffear-tmp-weapon/oracle.py', ROOT / 'tests/cryoffear-p345-weapon/oracle.py',
        ROOT / 'tests/cryoffear-ballistics/oracle.py', ROOT / 'tests/cryoffear-magazine-actions/run.py',
        args.environment.resolve(), args.proof.resolve(), args.sample.resolve()}
    proof = json.loads(args.proof.read_text('utf-8-sig'))
    dependencies.update(args.proof.parent / name for name in proof['archives'])
    with (output / 'cases.txt').open('w') as cases, (output / 'expected.txt').open('w') as expected:
        original = generate(args.sample, args.proof, cases, expected)
    dependencies.update((output / 'cases.txt', output / 'expected.txt'))
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/W4', '/WX',
        '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
        '/clang:-ffp-contract=off', '-Wno-overriding-option', '/Gy', '/Gw', '/I' + str(ROOT)]
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    temp = output / 'temp'
    temp.mkdir()
    env.update(TEMP=str(temp), TMP=str(temp))
    commands = []
    for index, source in enumerate(sources):
        dep = output / f'{index}.d'
        command = [str(args.compiler), *flags, '/clang:-M', '/clang:-MF', '/clang:' + str(dep), str(source)]
        commands.append(command)
        result = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, timeout=60)
        (output / f'{index}-headers.log').write_text(result.stdout + result.stderr, 'utf-8')
        if result.returncode:
            raise RuntimeError('Header discovery failed')
        make = dep.read_text().replace(chr(92) + chr(10), ' ')
        dependencies.update(Path(value.replace(chr(92) + ' ', ' ')).resolve()
            for value in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(path): sha(path) for path in sorted(dependencies)}
    compiler_hash = sha(args.compiler)
    executable = output / 'famas-shot.exe'
    command = [str(args.compiler), *flags, *map(str, sources), '/Fe:' + str(executable),
               '/link', '/machine:x86', '/threads:1', '/opt:ref']
    built = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, timeout=120)
    (output / 'compile.log').write_text(built.stdout + built.stderr, 'utf-8')
    tested = None
    negatives = []
    if not built.returncode:
        tested = subprocess.run([str(executable), str(output / 'cases.txt'), str(output / 'expected.txt')],
            cwd=output, capture_output=True, text=True, timeout=30)
        (output / 'test.log').write_text(tested.stdout + tested.stderr, 'utf-8')
    if tested is not None and not tested.returncode:
        cases = (output / 'cases.txt').read_text()
        answers = (output / 'expected.txt').read_text()
        for name, rows, expected, wanted in (
            ('empty', '', answers, 6), ('truncated', cases.splitlines()[0].rsplit(' ', 1)[0], answers, 3),
            ('wrong-result', cases, 'ffffffff ' + answers.split(' ', 1)[1], 5),
            ('extra-result', cases, answers + '00000000\n', 7)):
            case_path, answer_path = output / (name + '-cases.txt'), output / (name + '-expected.txt')
            case_path.write_text(rows)
            answer_path.write_text(expected)
            result = subprocess.run([str(executable), str(case_path), str(answer_path)], cwd=output,
                capture_output=True, text=True, timeout=30)
            (output / (name + '.log')).write_text(result.stdout + result.stderr, 'utf-8')
            negatives.append(dict(name=name, expected=wanted, actual=result.returncode, passed=result.returncode == wanted))
        stale = subprocess.run([str(executable), str(output / 'cases.txt'), str(output / 'expected.txt'), '--stale-angles'],
            cwd=output, capture_output=True, text=True, timeout=30)
        (output / 'stale-angles.log').write_text(stale.stdout + stale.stderr, 'utf-8')
        negatives.append(dict(name='pre-recoil-angle-snapshot', expected=5, actual=stale.returncode, passed=stale.returncode == 5))
    after = {str(path): sha(path) for path in sorted(dependencies)}
    success = not built.returncode and tested is not None and not tested.returncode and before == after and \
        compiler_hash == sha(args.compiler) and all(value['passed'] for value in negatives)
    receipt = dict(success=success, original=original, command=command, dependencyCommands=commands,
        dependencyBefore=before, dependencyAfter=after, compilerSha256=compiler_hash, negative=negatives,
        compiled=built.returncode, tested=tested.returncode if tested else None, sourceCopies=0,
        boundary='Pure typed FAMAS attack component only; no SDK host, complete weapon, deployment, gameplay or live performance acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(tested.stdout + tested.stderr if tested else built.stdout + built.stderr)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
