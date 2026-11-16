#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the real selector and compare the complete matched original function."""
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
    if output.exists() or not output.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    if any(p.is_symlink() or os.path.isjunction(p) for p in (output, *output.parents)):
        raise ValueError('Linked output')
    output.mkdir(parents=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    proof = json.loads(args.proof.read_text('utf-8-sig'))
    dependencies = {Path(__file__).resolve(), Path(__file__).with_name('oracle.py'),
        ROOT / 'tests/cryoffear-ballistics/oracle.py', ROOT / 'tests/cryoffear-magazine-actions/run.py',
        args.environment.resolve(), args.proof.resolve(), args.sample.resolve()}
    dependencies.update(args.proof.parent / name for name in proof['archives'])
    with (output / 'cases.txt').open('w') as cases, (output / 'expected.txt').open('w') as expected:
        original = generate(args.sample, args.proof, cases, expected)
    dependencies.update((output / 'cases.txt', output / 'expected.txt'))
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    temp = output / 'temp'
    temp.mkdir()
    env.update(TEMP=str(temp), TMP=str(temp))
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
        '/O2', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
        '/clang:-ffp-contract=off', '/Gy', '/Gw', '/I' + str(ROOT)]
    source = Path(__file__).with_name('main.cpp')
    dep = output / 'headers.d'
    command = [str(args.compiler), *flags, '/clang:-M', '/clang:-MF', '/clang:' + str(dep), str(source)]
    result = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, timeout=60)
    (output / 'headers.log').write_text(result.stdout + result.stderr, 'utf8')
    if result.returncode:
        raise RuntimeError('Header discovery failed')
    make = dep.read_text().replace(chr(92) + chr(10), ' ')
    dependencies.update(Path(v.replace(chr(92) + ' ', ' ')).resolve()
        for v in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(p): sha(p) for p in sorted(dependencies)}
    compiler_hash = sha(args.compiler)
    executable = output / 'flinch.exe'
    command = [str(args.compiler), *flags, str(source), '/Fe:' + str(executable),
        '/link', '/machine:x86', '/threads:1', '/opt:ref']
    built = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, timeout=120)
    (output / 'compile.log').write_text(built.stdout + built.stderr, 'utf8')
    tested = None
    negatives = []
    if not built.returncode:
        tested = subprocess.run([str(executable), str(output / 'cases.txt'), str(output / 'expected.txt')],
            cwd=output, capture_output=True, text=True, timeout=30)
        (output / 'test.log').write_text(tested.stdout + tested.stderr, 'utf8')
    if tested is not None and not tested.returncode:
        cases, answers = (output / 'cases.txt').read_text(), (output / 'expected.txt').read_text()
        for name, rows, expected, wanted in (
            ('empty', '', answers, 6),
            ('truncated', cases.splitlines()[0].rsplit(' ', 1)[0], answers, 3),
            ('wrong-result', cases, 'ffffffff ' + answers.split(' ', 1)[1], 5),
            ('extra-result', cases, answers + '00000000\n', 7)):
            case_path, answer_path = output / (name + '-cases.txt'), output / (name + '-expected.txt')
            case_path.write_text(rows)
            answer_path.write_text(expected)
            result = subprocess.run([str(executable), str(case_path), str(answer_path)], cwd=output,
                capture_output=True, text=True, timeout=30)
            (output / (name + '.log')).write_text(result.stdout + result.stderr, 'utf8')
            negatives.append(dict(name=name, expected=wanted, actual=result.returncode,
                                  passed=result.returncode == wanted))
    after = {str(p): sha(p) for p in sorted(dependencies)}
    success = not built.returncode and tested is not None and not tested.returncode and before == after and \
        compiler_hash == sha(args.compiler) and all(v['passed'] for v in negatives)
    receipt = dict(success=success, original=original, command=command, revision=revision,
        before=before, after=after, compilerSha256=compiler_hash, negative=negatives,
        compiled=built.returncode, tested=tested.returncode if tested is not None else None,
        sourceCopies=0, boundary='Exact selector only; no target task/schedule adapter, deployment or gameplay')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf8')
    print(tested.stdout + tested.stderr if tested is not None else built.stdout + built.stderr)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
