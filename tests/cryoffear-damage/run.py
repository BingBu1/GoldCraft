# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual CoF damage parser; verify transactional custom balance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a new build directory')
    output.mkdir(parents=True)
    inputs = [Path(__file__).resolve(), ROOT / 'tests/cryoffear-damage/main.cpp',
              ROOT / 'native/guest/cof/scenario_damage.cpp', ROOT / 'native/guest/cof/scenario_damage.hpp',
              ROOT / 'native/guest/cof/scenario_damage.json']

    def identities():
        return [dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                for p in inputs]

    before = identities()
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    rapidjson = ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'
    executable = output / 'cof-damage.exe'
    command = [str(args.compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20',
               '/EHsc', '/MT', '/utf-8', '/O2', '/clang:-O3', '/clang:-flto=thin',
               '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise', '/I' + str(ROOT),
               '/I' + str(rapidjson), str(inputs[1]), str(inputs[2]),
               '/Fe:' + str(executable), '/link', '/machine:x86', '/threads:1']
    compiled = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                              text=True, encoding='utf-8', errors='replace', timeout=120)
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, 'utf-8')
    tested = None
    if compiled.returncode == 0:
        tested = subprocess.run([str(executable), str(inputs[4])], cwd=output, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=30)
        (output / 'test.log').write_text(tested.stdout + tested.stderr, 'utf-8')
    after = identities()
    success = compiled.returncode == 0 and tested is not None and tested.returncode == 0 and before == after
    receipt = dict(command=command, compiled=compiled.returncode, tested=tested.returncode if tested else None,
                   before=before, after=after, unchanged=before == after, sourceCopies=0, success=success,
                   boundary='Host balance only; no weapon gameplay, deployment, balance or live performance acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print((tested.stdout + tested.stderr) if tested else (compiled.stdout + compiled.stderr))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
