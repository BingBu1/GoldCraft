# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile isolated actual phase/stamina modules and run behavioral boundaries."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
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
        raise ValueError('Use a fresh workspace build directory')
    out.mkdir(parents=True)
    domain = ROOT / 'native/csol/scenario/custom'
    sources = [ROOT / 'tests/csol-custom-stamina/main.cpp', domain / 'phase.cpp', domain / 'stamina.cpp']
    paths = [*sources, domain / 'phase.hpp', domain / 'stamina.hpp', Path(__file__).resolve(),
             args.compiler.resolve(), args.environment.resolve()]
    before = [identity(path) for path in paths]
    command = [str(args.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc',
               '/std:c++20', '/EHsc', '/MT', '/arch:AVX2', '/fp:precise',
               '/clang:-ffp-contract=off', '/clang:-O3', '/clang:-flto=thin',
               '/clang:-fuse-ld=lld', '/showIncludes', *map(str, sources),
               '/Fe:' + str(out / 'stamina.exe'), '/link', '/machine:x86',
               '/threads:1', '/opt:lldltojobs=1']
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig')) | {'VSLANG': '1033'}
    compiled = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, errors='replace')
    log = compiled.stdout + compiled.stderr
    (out / 'compile.log').write_text(log, encoding='utf-8')
    includes = sorted({Path(line.split('including file:', 1)[1].strip()).resolve()
                       for line in log.splitlines() if 'including file:' in line})
    include_before = [identity(path) for path in includes]
    run = subprocess.run([str(out / 'stamina.exe')], cwd=out, capture_output=True,
                         text=True, errors='replace') if compiled.returncode == 0 else None
    output = run.stdout + run.stderr if run is not None else ''
    (out / 'test.log').write_text(output, encoding='utf-8')
    after = [identity(path) for path in paths]
    include_after = [identity(path) for path in includes]
    stable = before == after and include_before == include_after
    summary = re.fullmatch(r'custom phase/stamina: ([1-9][0-9]*) checks passed\s*', output)
    success = compiled.returncode == 0 and run is not None and run.returncode == 0 and bool(summary) and stable
    receipt = dict(command=command, compile=compiled.returncode, test=run.returncode if run else None,
                   checks=int(summary[1]) if summary else None, success=success, inputsUnchanged=stable,
                   before=before, after=after, includesBeforeRun=include_before, includesAfterRun=include_after,
                   deployment=False, gameplay=False, livePerformance=False,
                   boundary='Actual pure modules only; not linked into client/server targets; includes recorded after compile')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: receipt[key] for key in ('compile', 'test', 'checks', 'success', 'inputsUnchanged')}))
    if not success:
        print(log[-4000:] if compiled.returncode else output)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
