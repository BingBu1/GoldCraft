# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and run the actual human-kick client input helper, without a source copy."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


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
    paths = [ROOT / 'native/csol/client/scenario/core/human_kick.hpp',
             ROOT / 'native/csol/scenario/core/human_kick.hpp',
             ROOT / 'tests/csol-human-kick-client/input.cpp', Path(__file__).resolve(),
             args.compiler.resolve(), args.environment.resolve()]
    before = [identity(path) for path in paths]
    command = [str(args.compiler.resolve()), '/nologo', '--target=i686-pc-windows-msvc',
               '/std:c++20', '/EHsc', '/MT', '/arch:AVX2', '/fp:precise',
               '/clang:-ffp-contract=off', '/clang:-O3', '/clang:-flto=thin',
               '/clang:-fuse-ld=lld', '/showIncludes',
               str(ROOT / 'tests/csol-human-kick-client/input.cpp'),
               '/Fe:' + str(out / 'input.exe'), '/link', '/machine:x86',
               '/threads:1', '/opt:lldltojobs=1']
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    compiled = subprocess.run(command, cwd=out, env=env, capture_output=True,
                              text=True, errors='replace')
    compile_log = compiled.stdout + compiled.stderr
    (out / 'compile.log').write_text(compile_log, encoding='utf-8')
    include_paths = sorted({Path(line.split('including file:', 1)[1].strip()).resolve()
                            for line in compile_log.splitlines() if 'including file:' in line})
    includes_before_run = [identity(path) for path in include_paths]
    run = subprocess.run([str(out / 'input.exe')], cwd=out, capture_output=True,
                         text=True, errors='replace') if compiled.returncode == 0 else None
    if run is not None:
        (out / 'test.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    after = [identity(path) for path in paths]
    includes_after_run = [identity(path) for path in include_paths]
    summary = re.fullmatch(r'human-kick client input: ([1-9][0-9]*) checks passed\s*',
                           run.stdout) if run is not None else None
    stable = before == after and includes_before_run == includes_after_run
    success = compiled.returncode == 0 and run is not None and run.returncode == 0 and bool(summary) and stable
    receipt = dict(command=command, compile=compiled.returncode,
                   test=run.returncode if run is not None else None,
                   checks=int(summary[1]) if summary else None,
                   before=before, after=after, inputsUnchanged=stable,
                   includesBeforeRun=includes_before_run, includesAfterRun=includes_after_run,
                   success=success, deployment=False, gameplay=False, livePerformance=False,
                   boundary='InputState only; actual includes sealed after compilation and across execution')
    if success:
        receipt['executable'] = identity(out / 'input.exe')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: receipt[key] for key in ('compile', 'test', 'checks', 'inputsUnchanged', 'success')}))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
