#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual ordinary-pool header in place and run original comparisons."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = path.resolve(strict=True)
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output', 'compiler', 'environment', 'client', 'evidence'):
        parser.add_argument('--' + name, type=Path, required=True)
    a = parser.parse_args()
    out = a.output.resolve()
    if out.exists():
        raise FileExistsError(out)
    out.mkdir(parents=True)
    source = Path(__file__).with_name('client.cpp')
    paths = [ROOT / 'native/csol/scenario/core/particle_runtime.hpp'] + [
        p for p in Path(__file__).parent.iterdir() if p.suffix in ('.py', '.cpp', '.md')]
    inputs = [identity(p) for p in paths]
    controls = [identity(a.environment), identity(a.compiler)]
    env = os.environ | json.loads(a.environment.read_text('utf8'))
    env.update(TEMP=str(out), TMP=str(out), VSLANG='1033', PYTHONDONTWRITEBYTECODE='1')
    exe = out / 'client.exe'
    command = [str(a.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc',
               '-fuse-ld=lld', '/MT', '/std:c++20', '/arch:AVX2', '/clang:-O3',
               '/clang:-flto=thin', '/fp:precise', '/clang:-ffp-contract=off',
               '/EHsc', '/showIncludes', '/I' + str(ROOT), '/Fo' + str(out / 'client.obj'),
               '/Fe' + str(exe), str(source), '/link', '/machine:x86', '/OPT:REF', '/OPT:ICF',
               '/threads:1']
    build = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True,
                           encoding='utf8', errors='replace')
    (out / 'compile.log').write_text(build.stdout + build.stderr, 'utf8')
    compiled = [source.resolve()]
    for match in re.finditer(r'Note: including file:\s*(.*?)\s*$', build.stdout, re.M):
        p = Path(match.group(1)).resolve()
        if p.is_relative_to(ROOT):
            compiled.append(p)
    sealed = {str(Path(x['path'])).casefold() for x in inputs}
    assert all(str(p).casefold() in sealed for p in compiled), 'Unsealed compile input'
    tests = []
    if build.returncode == 0:
        commands = [[str(exe)], [sys.executable, str(Path(__file__).with_name('oracle.py')),
                    '--executable', str(exe), '--client', str(a.client.resolve()),
                    '--evidence', str(a.evidence.resolve()), '--output', str(out / 'oracle.json')]]
        for index, cmd in enumerate(commands):
            result = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True,
                                    encoding='utf8', errors='replace', timeout=180)
            (out / f'test-{index}.log').write_text(result.stdout + result.stderr, 'utf8')
            tests.append(dict(command=cmd, exit=result.returncode, stdout=result.stdout,
                              stderr=result.stderr))
            if result.returncode:
                break
    unchanged = all(identity(Path(x['path'])) == x for x in inputs + controls)
    receipt = dict(success=build.returncode == 0 and len(tests) == 2 and
                   all(x['exit'] == 0 for x in tests) and unchanged,
                   revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   compileExit=build.returncode, command=command, controls=controls, inputs=inputs,
                   compiledIncludes=[identity(p) for p in sorted(set(compiled))],
                   inputsUnchanged=unchanged, tests=tests, sourceCopies=0,
                   artifact=identity(exe) if exe.exists() else None, runtimeIntegrated=False,
                   deployed=False, liveGameplay=False, performanceAccepted=False)
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps({k: receipt[k] for k in ('success', 'compileExit', 'tests', 'inputsUnchanged')}))
    return 0 if receipt['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
