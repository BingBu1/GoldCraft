#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the in-place Sliding helper with precise x86 production options."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--compiler', type=Path, required=True)
    ap.add_argument('--environment', type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise FileExistsError(out)
    out.mkdir(parents=True)
    environment = args.environment.resolve(strict=True)
    compiler = args.compiler.resolve(strict=True)
    before = {str(path.resolve()).casefold(): identity(path) for directory in
              (ROOT / 'native/csol/scenario/bosses', Path(__file__).parent)
              for path in directory.rglob('*') if path.is_file() and path.suffix in ('.hpp', '.h', '.cpp', '.py')}
    controls = [identity(environment), identity(compiler)]
    env = os.environ | json.loads(environment.read_text('utf8'))
    env.update(TEMP=str(out), TMP=str(out), VSLANG='1033')
    source = Path(__file__).with_name('client.cpp')
    exe = out / 'client.exe'
    command = [str(compiler), '/nologo', '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
               '/std:c++20', '/arch:AVX2', '/clang:-O3', '/clang:-flto=thin', '/fp:precise',
               '/clang:-ffp-contract=off', '/EHsc', '/showIncludes', '/I' + str(ROOT),
               '/Fo' + str(out / 'client.obj'), '/Fe' + str(exe), str(source),
               '/link', '/machine:x86', '/OPT:REF', '/OPT:ICF', '/threads:1']
    result = subprocess.run(command, cwd=ROOT, env=env, text=True, encoding='utf8', errors='replace', capture_output=True)
    (out / 'compile.log').write_text(result.stdout + result.stderr, 'utf8')
    included = {str(source.resolve()).casefold(), str(Path(__file__).resolve()).casefold()}
    for match in re.finditer(r'Note: including file:\s*(.*?)\s*$', result.stdout, re.M):
        path = Path(match.group(1)).resolve()
        if path.is_relative_to(ROOT):
            included.add(str(path).casefold())
    inputs = [before[key] for key in sorted(included)]
    test = None
    if result.returncode == 0:
        run = subprocess.run([str(exe)], cwd=ROOT, env=env, text=True, capture_output=True)
        (out / 'test.log').write_text(run.stdout + run.stderr, 'utf8')
        test = dict(exit=run.returncode, output=run.stdout, error=run.stderr)
    unchanged = all(identity(Path(row['path'])) == row for row in inputs + controls)
    receipt = dict(success=result.returncode == 0 and test is not None and test['exit'] == 0 and unchanged,
                   revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   command=command, compileExit=result.returncode, test=test, inputs=inputs, controls=controls,
                   inputsUnchanged=unchanged, sourceCopies=0, artifact=identity(exe) if exe.exists() else None,
                   runtimeIntegrated=False, deployed=False, liveGameplay=False, performanceAccepted=False)
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps({key: receipt[key] for key in ('success', 'compileExit', 'test', 'inputsUnchanged')}))
    return 0 if receipt['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
