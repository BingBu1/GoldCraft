#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run real scene/admission TUs and point-light backend on an isolated WGL context."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('tests/cryoffear-scene-admission/main.cpp',
           'native/guest/cof/client/point_light_render.cpp',
           'native/guest/cof/client/muzzle_flash.cpp')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('environment', 'commands', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (out, *out.parents)):
        raise ValueError('Use a fresh workspace build directory')
    out.mkdir(parents=True)
    config = ROOT / '.tools/clang-toolchain.json'
    compiler = Path(json.loads(config.read_text('utf-8-sig'))['root']) / 'bin/clang-cl.exe'
    database = json.loads(args.commands.read_text('utf8'))
    command = next(row['command'] for row in database if row['file'].replace(chr(92), '/').endswith('/native/csol/client/plugin.cpp'))
    tokens = re.findall(r'"[^"\r\n]*"|[^\s]+', command)
    flags = ['/nologo', '/TP', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/W3', '/WX',
             '--target=i686-pc-windows-msvc', '/O2', '/clang:-O3', '/clang:-flto=thin',
             '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off', '-Wno-overriding-option',
             '/DNDEBUG', *[s for s in tokens if s.startswith('-D')],
             *['/imsvc' + s[2:] for s in tokens if s.startswith('-I')]]
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    temp = out / 'temp'
    temp.mkdir()
    env.update(TEMP=str(temp), TMP=str(temp))
    inputs = {Path(__file__).resolve(), args.environment.resolve(), args.commands.resolve(),
              config, compiler, compiler.with_name('lld-link.exe')}
    for i, name in enumerate(SOURCES):
        dep = out / f'{i}.d'
        result = subprocess.run([str(compiler), *flags, '/clang:-M', '/clang:-MF',
                                 '/clang:' + str(dep), str(ROOT / name)],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
        (out / f'headers-{i}.log').write_text(result.stdout + result.stderr, 'utf8')
        result.check_returncode()
        make = dep.read_text().replace(chr(92) + chr(10), ' ')
        inputs.update(Path(p.replace(chr(92) + ' ', ' ')).resolve()
                      for p in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(p): sha(p) for p in sorted(inputs)}
    executable = out / 'scene-admission.exe'
    argv = [str(compiler), *flags, '/clang:-fuse-ld=lld', *[str(ROOT / s) for s in SOURCES],
            '/Fe:' + str(executable), '/link', '/machine:x86', '/threads:1', '/opt:ref',
            'opengl32.lib', 'user32.lib', 'gdi32.lib']
    built = subprocess.run(argv, cwd=out, env=env, capture_output=True, text=True, timeout=150)
    (out / 'compile.log').write_text(built.stdout + built.stderr, 'utf8')
    tested = None
    if built.returncode == 0:
        tested = subprocess.run([str(executable)], cwd=out, capture_output=True, text=True, timeout=45)
        (out / 'test.log').write_text(tested.stdout + tested.stderr, 'utf8')
    after = {str(p): sha(p) for p in sorted(inputs)}
    success = built.returncode == 0 and tested is not None and tested.returncode == 0 and before == after
    (out / 'receipt.json').write_text(json.dumps(dict(success=success, command=argv,
        before=before, after=after, unchanged=before == after, compiled=built.returncode,
        tested=tested.returncode if tested else None, sourceCopies=0, gameplay=False, performance=False,
        boundary='Actual scene/admission TUs and real point-light renderer; other services controlled'), indent=2), 'utf8')
    print(tested.stdout + tested.stderr if tested else built.stdout + built.stderr, flush=True)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
