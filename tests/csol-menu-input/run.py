# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and run the production host menu input helper."""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def identity(path):
    return dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build output')
    out.mkdir(parents=True)
    sources = [ROOT / 'native/csol/client/menu_input.hpp', ROOT / 'tests/csol-menu-input/main.cpp', Path(__file__)]
    before = [identity(path) for path in sources]
    command = [str(args.compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/O2',
               '/I' + str(ROOT), str(ROOT / 'tests/csol-menu-input/main.cpp'), '/Fe:' + str(out / 'input.exe'),
               '/link', '/machine:x86']
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    compiled = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, errors='replace')
    (out / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    run = subprocess.run([str(out / 'input.exe')], cwd=out, capture_output=True, text=True) if compiled.returncode == 0 else None
    if run:
        (out / 'test.log').write_text(run.stdout + run.stderr)
    after = [identity(path) for path in sources]
    success = compiled.returncode == 0 and run is not None and run.returncode == 0 and before == after
    (out / 'receipt.json').write_text(json.dumps(dict(command=command, compile=compiled.returncode,
        test=run.returncode if run else None, before=before, after=after, unchanged=before == after, success=success), indent=2) + '\n')
    print(compiled.stdout + compiled.stderr)
    if run:
        print(run.stdout + run.stderr)
    raise SystemExit(0 if success else 1)

if __name__ == '__main__':
    main()
