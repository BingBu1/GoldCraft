#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify actual IFileSystem009 with real Win32 streams, search paths and PAK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('out', 'compiler', 'environment', 'dll', 'resources'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.out.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        parser.error('output must be fresh under workspace build')
    if not args.dll.resolve().is_relative_to(ROOT / 'build'):
        parser.error('only a workspace build DLL can be tested')
    output.mkdir(parents=True)
    sdk = ROOT / 'external/ReHLDS/rehlds'
    exe = output / 'fixture.exe'
    command = [str(args.compiler), '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/std:c++20', '/O2',
        '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off',
        '/EHsc', '/MT', '/nologo', '/showIncludes', '/I' + str(ROOT / 'native/include'),
        '/I' + str(sdk / 'common'), '/I' + str(sdk / 'public'), '/I' + str(sdk / 'public/rehlds'),
        str(HERE / 'main.cpp'), '/Fo' + str(output / 'fixture.obj'), '/Fe' + str(exe), '/link',
        '/OPT:REF', '/OPT:ICF', '/OPT:LLDLTO=3', '/threads:1']
    inputs = [HERE / 'main.cpp', Path(__file__), ROOT / 'native/include/goldcraft/open_file_metadata.hpp',
        sdk / 'public/FileSystem.h', args.compiler, args.environment, args.dll, args.resources]
    before = [identity(path) for path in inputs]
    resources = [Path(line).resolve(strict=True) for line in args.resources.read_text('utf-8').splitlines()]
    if not resources or any(not path.is_relative_to(ROOT / 'sandbox') for path in resources):
        raise ValueError('only workspace sandbox resources')
    stamps = [(path.stat().st_size, path.stat().st_mtime_ns, path.stat().st_ino) for path in resources]
    commands = [('compile', command), ('test', [str(exe), str(args.dll.resolve()), str(output / 'fixture'), str(args.resources.resolve())])]
    plan = dict(before=before, command=commands, resources=len(resources), resourceContentRead='Four bytes per file plus native VFS open',
        boundary='Actual filesystem component and offline timing; no client/server performance acceptance')
    (output / 'plan.json').write_text(json.dumps(plan, indent=2), 'utf-8')
    environment = dict(os.environ)
    environment.update(json.loads(args.environment.read_text('utf-8-sig')))
    result = dict(success=False, steps=[])
    try:
        for label, argv in commands:
            began = time.monotonic()
            with (output / (label + '.log')).open('wb') as log:
                run = subprocess.run(argv, cwd=output, env=environment, stdout=log, stderr=subprocess.STDOUT,
                    timeout=180, creationflags=subprocess.CREATE_NO_WINDOW)
            result['steps'].append(dict(name=label, exit=run.returncode, seconds=time.monotonic()-began))
            if run.returncode:
                raise RuntimeError(label + ' failed; inspect log')
        result['inputsUnchanged'] = before == [identity(path) for path in inputs]
        result['resourceStampsUnchanged'] = stamps == [(path.stat().st_size, path.stat().st_mtime_ns, path.stat().st_ino) for path in resources]
        if not result['inputsUnchanged'] or not result['resourceStampsUnchanged']:
            raise RuntimeError('inputs changed')
        result['output'] = (output / 'test.log').read_text('utf-8')
        result['success'] = True
    except Exception as error:
        result['error'] = str(error)
    finally:
        (output / 'receipt.json').write_text(json.dumps(result, indent=2), 'utf-8')
    print(json.dumps(result))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
