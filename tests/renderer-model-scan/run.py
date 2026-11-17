#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise actual host media inspection and Renderer bulk brush enumeration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / 'external/MetaHookSv-20261007/Plugins/Renderer'


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))


def block(text, start):
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--renderer-command', type=Path, required=True)
    parser.add_argument('--old-scan', action='store_true', help='Negative case: execute the actual pinned upstream world scan')
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Fresh workspace build output required')
    command = json.loads(args.renderer_command.read_text('utf-8-sig'))['argv']
    compiler = Path(command[0])
    include_args = [x for x in command[1:] if x.startswith(('-I', '/I', '-imsvc'))]
    paths = {Path(__file__), Path(__file__).with_name('main.cpp'), args.environment, args.renderer_command, compiler,
             compiler.with_name('lld-link.exe'), ROOT / 'sources.lock.json'}
    for folder in ('native/client', 'native/include', 'external/MetaHookSv-20261007/MetaHook/include',
                   'external/MetaHookSv-20261007/Plugins/Renderer/src',
                   'external/MetaHookSv-20261007/MetaHook/thirdparty/capstone_fork/include',
                   'external/MetaHookSv-20261007/MetaHook/thirdparty/cache/VC-LTL-5.3.1/TargetPlatform'):
        paths.update(p for p in (ROOT / folder).rglob('*') if p.is_file() and p.suffix in ('.cpp', '.h', '.hpp', '.inc'))
    before = [identity(p) for p in sorted(paths)]
    text = (RENDERER / 'src/gl_rmain.cpp').read_text('utf-8-sig')
    dispatcher = block(text, text.index('void EngineForEachClientBrushModel('))
    origin = dict(kind='actual in-place dispatcher', source=identity(RENDERER / 'src/gl_rmain.cpp'))
    for filename, name in (('gl_rsurf.cpp', 'GL_BuildLightmaps'), ('gl_wsurf.cpp', 'R_LoadWorldResources'),
                           ('gl_rmain.cpp', 'R_Reload_f')):
        body = (RENDERER / 'src' / filename).read_text('utf-8-sig')
        body = block(body, body.index('void ' + name + '('))
        if body.count('EngineForEachClientBrushModel(') != 1 or 'hudGetModelByIndex' in body:
            raise ValueError('Bulk scan not routed through actual dispatcher: ' + name)
    if args.old_scan:
        lock = json.loads((ROOT / 'sources.lock.json').read_text('utf-8'))['sources']['Renderer']
        previous = subprocess.check_output(['git', 'show', lock['commit'] + ':src/gl_wsurf.cpp'], cwd=RENDERER).decode('utf-8-sig')
        start = previous.index('void R_LoadWorldResources(')
        loop = block(previous, previous.index('for (int j = 1;', start))
        assert loop.count('R_GetWorldSurfaceModel(mod);') == 1
        dispatcher = 'void EngineForEachClientBrushModel(void (*consumer)(model_t*)) {\n' + loop.replace('R_GetWorldSurfaceModel(mod);', 'consumer(mod);') + '\n}'
        origin = dict(kind='pinned upstream world scan; only sink adapted', commit=lock['commit'],
                      loopSha256=hashlib.sha256(loop.encode()).hexdigest())
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold(): del env[old]
        env[key] = value
    out.mkdir()
    generated = out / 'renderer_model_scan.inc'
    generated.write_text(dispatcher + '\n', 'utf-8')
    obj, exe = out / 'scan.obj', out / 'scan.exe'
    compile_args = [str(compiler), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/O2',
                    '/clang:-O3', '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off', '/Gy', '/Gw',
                    '/EHsc', '/MT', '/utf-8', '/DNOMINMAX', '/DWIN32', '/D_WINDOWS', '/DNO_MALLOC_OVERRIDE',
                    '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(out), *include_args, '/showIncludes',
                    '/c', str(Path(__file__).with_name('main.cpp')), '/Fo' + str(obj)]
    link_args = [str(compiler.with_name('lld-link.exe')), '/machine:x86', '/subsystem:console',
                 '/OPT:REF', '/threads:1', str(obj), str(out / 'cache.obj'), 'opengl32.lib', '/out:' + str(exe)]
    cache_args = [str(ROOT / 'native/client/resource_model_cache.cpp') if x == str(Path(__file__).with_name('main.cpp'))
                  else '/Fo' + str(out / 'cache.obj') if x == '/Fo' + str(obj) else x for x in compile_args]
    result = dict(success=False, sourceCopies=0, gameplay=False, performance=False,
                  boundary='actual production inspection and dispatcher with controlled engine/sink boundaries',
                  inputs=before, origin=origin, steps=[])
    try:
        for name, argv in (('compile', compile_args), ('compile-cache', cache_args), ('link', link_args), ('test', [str(exe)])):
            job = subprocess.run(argv, cwd=ROOT, env=env, capture_output=True, timeout=120)
            output = (job.stdout + job.stderr).decode('utf-8', 'replace')
            (out / (name + '.log')).write_text(output, 'utf-8')
            result['steps'].append(dict(name=name, command=argv, exit=job.returncode))
            if name.startswith('compile') and job.returncode == 0:
                actual = []
                for p in re.findall(r'Note: including file:\s*([^\r\n]+)', output):
                    p = Path(p.strip()).resolve(strict=True)
                    if p.is_relative_to(ROOT) and p != generated:
                        if p not in paths:
                            raise ValueError('Unrecorded actual project include: ' + str(p))
                        actual.append(str(p))
                result['actualIncludes'] = sorted(set(result.get('actualIncludes', []) + actual))
            if name == 'test':
                result['test'] = json.loads(next(line for line in output.splitlines() if line.startswith('{')))
                print(output.strip())
                if args.old_scan:
                    assert job.returncode != 0 and result['test']['failures'] > 0
                    result['negativeDetected'] = True
                    continue
            if job.returncode:
                raise RuntimeError(name + ': ' + output[-6000:])
        assert [identity(p) for p in sorted(paths)] == before
        result.update(success=True, inputsUnchanged=True, generated=identity(generated), artifact=identity(exe))
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        (out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', 'utf-8')


if __name__ == '__main__':
    main()
