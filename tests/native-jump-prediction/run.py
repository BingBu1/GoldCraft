#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a real-DLL PM probe without starting a game client or server."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
NATIVE_SHA = 'b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2'


def identity(path):
    return dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('compiler', 'environment', 'native', 'dependencies', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    sample, dependencies = args.native.resolve(strict=True), args.dependencies.resolve(strict=True)
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not sample.is_relative_to(ROOT / 'analysis'):
        raise ValueError('Use a private analysis sample and fresh build output')
    if not dependencies.is_relative_to(ROOT / 'sandbox'):
        raise ValueError('Use only isolated sandbox DLL dependencies')
    if identity(sample)['sha256'] != NATIVE_SHA:
        raise ValueError('Unmatched native client')
    out.mkdir()
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    sdk = ROOT / 'external/MetaHookSv-20261007/MetaHook/include/HLSDK'
    source = Path(__file__).with_name('probe.cpp')
    inputs = [identity(p.resolve()) for p in (source, Path(__file__), sample, args.compiler, args.environment,
        ROOT / 'native/csol/scenario/custom/jump_prediction.hpp', sdk / 'pm_shared/pm_defs.h',
        sdk / 'pm_shared/pm_movevars.h', sdk / 'common/usercmd.h', sdk / 'common/pmtrace.h')]
    record = dict(success=False, inputs=inputs, gameProcess=False, deployed=False, steps=[],
        boundary='Actual x86 native DLL PM on a flat engine-callback floor; not real input/network/frame-time acceptance')
    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=out, env=env, capture_output=True, timeout=60)
        text = (result.stdout + result.stderr).decode('utf-8', 'replace')
        (out / (name + '.log')).write_text(text, encoding='utf-8')
        record['steps'].append(dict(name=name, exit=result.returncode, command=list(map(str, command))))
        print(name, result.returncode, text[-5000:], flush=True)
        if result.returncode:
            raise RuntimeError(name + ' failed')
        return text
    try:
        executable = out / 'probe.exe'
        command = [args.compiler, '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT',
                   '/O2', '/arch:AVX2', '/fp:precise', '/utf-8', '-fuse-ld=lld',
                   '/I' + str(sdk / 'pm_shared'), '/I' + str(sdk / 'common'),
                   source, '/Fo' + str(out / 'probe.obj'), '/Fe' + str(executable),
                   '/link', '/machine:x86', '/threads:1', 'kernel32.lib']
        run('build', command)
        metadata = run('layout', [executable])
        (out / 'layout.json').write_text(metadata, encoding='utf-8')
        run('native-prediction', [executable, sample, dependencies])
        record['success'] = True
    finally:
        record['inputsUnchanged'] = inputs == [identity(Path(p['path'])) for p in inputs]
        record['success'] &= record['inputsUnchanged']
        (out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
