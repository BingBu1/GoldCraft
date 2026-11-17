#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run actual Renderer texture-label functions on a hidden Windows GL context."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'external/MetaHookSv-20261007/Plugins/Renderer/src'
SDK = ROOT / 'external/MetaHookSv-20261007/MetaHook/include/HLSDK/common'


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))


def function(text, name):
    start = text.index('void ' + name + '(')
    opening = text.index('{', start)
    depth, at = 1, opening + 1
    while depth:
        depth += (text[at] == '{') - (text[at] == '}')
        at += 1
    return text[start:at]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--original-labels', action='store_true', help='Replay the pinned unguarded functions as a negative case')
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Fresh workspace build output required')
    paths = [Path(__file__), Path(__file__).with_name('main.cpp'), SDK / 'cvardef.h',
             SOURCE / 'gl_rmisc.cpp', SOURCE / 'gl_rmain.cpp', SOURCE / 'gl_local.h', args.compiler, args.environment]
    before = [identity(path) for path in paths]
    text = (SOURCE / 'gl_rmisc.cpp').read_text('utf-8-sig')
    origin = dict(kind='current in-place source', path=str(SOURCE / 'gl_rmisc.cpp'))
    if args.original_labels:
        entry = json.loads((ROOT / 'sources.lock.json').read_text('utf-8'))['sources']['Renderer']
        text = subprocess.check_output(['git', 'show', entry['commit'] + ':src/gl_rmisc.cpp'],
                                       cwd=ROOT / entry['path']).decode('utf-8')
        origin = dict(kind='pinned upstream', commit=entry['commit'], path='src/gl_rmisc.cpp',
                      sha256=hashlib.sha256(text.encode('utf-8')).hexdigest())
    functions = '\n'.join(function(text, name) for name in ('GL_SetTextureDebugName', 'GL_SetTextureDebugNameFormat'))
    registration = (SOURCE / 'gl_rmain.cpp').read_text('utf-8-sig')
    if 'pfnRegisterVariable("r_debug_texture_names", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE)' not in registration:
        raise ValueError('Actual default registration differs')
    out.mkdir()
    generated = out / 'texture_label_functions.inc'
    generated.write_text(functions + '\n', 'utf-8')
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for existing in list(env):
            if existing.casefold() == key.casefold(): del env[existing]
        env[key] = value
    obj, exe = out / 'texture-labels.obj', out / 'texture-labels.exe'
    compile_command = [str(args.compiler), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20',
        '/O2', '/clang:-O3', '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off', '/MT',
        '/EHsc', '/showIncludes', '/I' + str(SDK), '/I' + str(out), '/c',
        str(Path(__file__).with_name('main.cpp')), '/Fo' + str(obj)]
    link_command = [str(args.compiler.with_name('lld-link.exe')), '/machine:x86', '/subsystem:console',
                    str(obj), 'opengl32.lib', 'gdi32.lib', 'user32.lib', '/out:' + str(exe)]
    result = dict(success=False, sourceCopies=0, gameplay=False, performanceClaim='Hidden GL diagnostic only',
                  inputs=before, generated=identity(generated), functionOrigin=origin, steps=[])
    try:
        for name, command in (('compile', compile_command), ('link', link_command), ('gl', [str(exe)])):
            start = time.monotonic()
            job = subprocess.run(command, env=env, cwd=ROOT, capture_output=True, timeout=120)
            output = (job.stdout + job.stderr).decode('utf-8', errors='replace')
            (out / (name + '.log')).write_text(output, 'utf-8')
            result['steps'].append(dict(name=name, exit=job.returncode, seconds=time.monotonic() - start))
            if job.returncode:
                raise RuntimeError(name + ': ' + output[-2500:])
            if name == 'compile':
                for path in re.findall(r'Note: including file:\s*([^\r\n]+)', output):
                    path = Path(path.strip()).resolve(strict=True)
                    if path.is_relative_to(ROOT) and path not in paths and path != generated:
                        raise ValueError('Unexpected project header: ' + str(path))
            if name == 'gl':
                result['results'] = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
                assert result['results'][-1]['passed']
        assert [identity(path) for path in paths] == before
        result.update(success=True, inputsUnchanged=True, artifact=identity(exe))
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        (out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', 'utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('inputs', 'generated')}))


if __name__ == '__main__':
    main()
