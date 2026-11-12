#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual CoF core/compatibility rendering and compare real GPU pixels."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('tests/cryoffear-core-render/main.cpp', 'native/guest/cof/client/custom_decal.cpp',
           'native/guest/cof/client/point_light_render.cpp')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (out, *out.parents)):
        raise ValueError('Use a fresh workspace build directory')
    out.mkdir(parents=True)
    temporary = out / 'temp'
    temporary.mkdir()
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    env.update(TEMP=str(temporary), TMP=str(temporary))
    config = ROOT / '.tools/clang-toolchain.json'
    compiler = Path(json.loads(config.read_text('utf-8-sig'))['root']) / 'bin/clang-cl.exe'
    sdk = ROOT / 'external/MetaHookSv-20261007/MetaHook/include'
    includes = [sdk, sdk / 'Interface', *[sdk / 'HLSDK' / d
        for d in ('cl_dll', 'common', 'engine', 'pm_shared', 'public')]]
    flags = ['/nologo', '/TP', '/std:c++20', '/EHsc', '/MT', '/DNOMINMAX', '/W4', '/WX',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
             '/clang:-ffp-contract=off', '-Wno-overriding-option', '--target=i686-pc-windows-msvc',
             *['/imsvc' + str(p) for p in includes]]

    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=out, env=env,
                                capture_output=True, text=True, timeout=90)
        (out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        result.check_returncode()
        return result

    inputs = {Path(__file__).resolve(), config, compiler, compiler.with_name('lld-link.exe'),
              args.environment.resolve()}
    for number, source in enumerate(SOURCES):
        dep = out / f'{number}.d'
        run(f'headers-{number}', [compiler, *flags, '/clang:-M', '/clang:-MF',
                                 '/clang:' + str(dep), ROOT / source])
        make = dep.read_text().replace('\\\n', ' ')
        inputs.update(Path(p.replace('\\ ', ' ')).resolve()
                      for p in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(p): sha(p) for p in sorted(inputs)}
    executable = out / 'core-render.exe'
    run('compile', [compiler, *flags, '/clang:-fuse-ld=lld', *[ROOT / s for s in SOURCES],
        '/Fe:' + str(executable), '/link', '/machine:x86', '/threads:1', '/opt:ref',
        'opengl32.lib', 'user32.lib', 'gdi32.lib'])
    result = run('gpu', [executable])
    after = {str(p): sha(p) for p in sorted(inputs)}
    receipt = dict(success=before == after, before=before, after=after, result=result.stdout,
        sourceCopies=0, actualCoreAndCompatibility=True, gameplay=False, performance=False)
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if before != after:
        raise RuntimeError('Inputs changed during GPU validation')
    print(result.stdout, end='')


if __name__ == '__main__':
    main()
