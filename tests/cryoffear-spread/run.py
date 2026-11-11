# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile portable spread and compare it with private original x86 execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from oracle import generate

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--original-sample', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a new build directory')
    output.mkdir(parents=True)
    inputs = [Path(__file__).resolve(), ROOT / 'tests/cryoffear-spread/oracle.py',
              ROOT / 'tests/cryoffear-spread/main.cpp', ROOT / 'native/guest/cof/spread.cpp',
              ROOT / 'native/guest/cof/spread.hpp']

    def identities():
        return [dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                for p in inputs]

    before = identities()
    with (output / 'oracle.txt').open('w', encoding='ascii') as oracle:
        original = generate(args.original_sample, oracle)
    (output / 'oracle-receipt.json').write_text(json.dumps(original, indent=2), 'utf-8')
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    executable = output / 'cof-spread.exe'
    command = [str(args.compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20',
               '/EHsc', '/MT', '/utf-8', '/O2', '/clang:-O3', '/clang:-flto=thin',
               '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise', '/I' + str(ROOT),
               str(inputs[2]), str(inputs[3]), '/Fe:' + str(executable),
               '/link', '/machine:x86', '/threads:1']
    compiled = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                              text=True, encoding='utf-8', errors='replace', timeout=120)
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, 'utf-8')
    tested = None
    if compiled.returncode == 0:
        tested = subprocess.run([str(executable), str(output / 'oracle.txt')], cwd=output,
                                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
        (output / 'test.log').write_text(tested.stdout + tested.stderr, 'utf-8')
    after = identities()
    success = compiled.returncode == 0 and tested is not None and tested.returncode == 0 and before == after
    receipt = dict(command=command, compiled=compiled.returncode, tested=tested.returncode if tested else None,
                   before=before, after=after, unchanged=before == after, sourceCopies=0,
                   original=original, success=success,
                   boundary='Portable update only; no shot/lifecycle integration, DLL build, deployment, gameplay or live performance acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print((tested.stdout + tested.stderr) if tested else (compiled.stdout + compiled.stderr))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
