#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual bounded ledger/account-scope components; never deploy a server."""
import argparse
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('A fresh workspace build directory is required')
    sources = [ROOT / 'tests/csol-damage-refund/main.cpp',
               ROOT / 'native/csol/scenario/custom/refund_cap.cpp',
               ROOT / 'native/csol/server/custom/damage_refund_account.cpp']
    paths = [*sources, ROOT / 'native/csol/scenario/custom/refund_cap.hpp',
             ROOT / 'native/csol/server/custom/damage_refund_account.h', Path(__file__).resolve(),
             args.compiler.resolve(), args.environment.resolve(), args.compiler.with_name('lld-link.exe').resolve()]
    before = [identity(path) for path in paths]
    out.mkdir(parents=True)
    environment = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(environment):
            if old.casefold() == key.casefold():
                del environment[old]
        environment[key] = value
    command = [str(args.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc',
               '/std:c++20', '/EHsc', '/MT', '/arch:AVX2', '/fp:precise',
               '/clang:-ffp-contract=off', '/clang:-O3', '/clang:-flto=thin',
               '/clang:-fuse-ld=lld', '/showIncludes', *map(str, sources),
               '/Fe:' + str(out / 'refund.exe'), '/link', '/machine:x86',
               '/threads:1', '/opt:lldltojobs=1']
    compiled = subprocess.run(command, cwd=out, env=environment, capture_output=True, timeout=180)
    log = (compiled.stdout + compiled.stderr).decode('utf8', errors='replace')
    (out / 'compile.log').write_text(log, 'utf8')
    includes = sorted({Path(line.split('including file:', 1)[1].strip()).resolve()
                       for line in log.splitlines() if 'including file:' in line})
    included = [identity(path) for path in includes]
    run = subprocess.run([str(out / 'refund.exe')], cwd=out, capture_output=True, timeout=30) if compiled.returncode == 0 else None
    output = (run.stdout + run.stderr).decode('utf8', errors='replace') if run else ''
    (out / 'test.log').write_text(output, 'utf8')
    stable = before == [identity(path) for path in paths] and included == [identity(path) for path in includes]
    summary = re.fullmatch(r'damage refund: ([1-9][0-9]*) checks passed\s*', output)
    success = compiled.returncode == 0 and run is not None and run.returncode == 0 and bool(summary) and stable
    receipt = dict(success=success, inputsUnchanged=stable, command=command, compile=compiled.returncode,
                   test=run.returncode if run else None, checks=int(summary[1]) if summary else None,
                   inputs=before, includes=included, sourceCopies=0, deployment=False, gameplay=False,
                   boundary='Actual ledger and stack adapter only; native account hook and whole-server validation separate')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps({key: receipt[key] for key in ('success', 'inputsUnchanged', 'compile', 'test', 'checks')}))
    if not success:
        print(log[-4000:] if compiled.returncode else output)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
