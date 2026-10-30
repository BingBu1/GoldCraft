#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and exercise the real scenario HUD decoders, state and message chain."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def identity(path):
    return {'path': path.relative_to(ROOT).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', required=True, help='clang-cl executable path or PATH command')
    parser.add_argument('--output', type=Path, required=True, help='New output directory under workspace build')
    parser.add_argument('--metahook', type=Path, default=ROOT / 'external/MetaHookSv', help='MetaHookSv source root')
    parser.add_argument('--environment', type=Path, help='Optional JSON containing INCLUDE, LIB and LIBPATH')
    args = parser.parse_args()
    output = args.output.absolute()
    if not output.resolve().is_relative_to(ROOT / 'build'):
        parser.error('Output must remain under workspace build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('Linked output refused')
    if output.exists():
        parser.error('Use a new output directory to preserve previous test evidence')
    clang = shutil.which(args.clang)
    if not clang:
        parser.error('clang-cl was not found')
    env = os.environ.copy()
    if args.environment:
        config = json.loads(args.environment.read_text(encoding='utf-8-sig'))
        for name in ['INCLUDE', 'LIB', 'LIBPATH']:
            env[name] = config[name]
    sdk_root = args.metahook.absolute()
    sdk = sdk_root / 'MetaHook/include'
    if not (sdk / 'metahook.h').is_file():
        parser.error('MetaHookSv headers are missing; prepare the project dependencies first')
    includes = [ROOT, sdk, sdk/'Interface', sdk/'VGUI', sdk/'Interface/VGUI',
        sdk_root/'Plugins/VGUI2Extension/include/Interface',
        *[sdk/part for part in ['HLSDK/cl_dll', 'HLSDK/common', 'HLSDK/engine',
            'HLSDK/pm_shared', 'HLSDK/public', 'SourceSDK', 'SourceSDK/tier0', 'SourceSDK/tier1']],
        sdk_root/'thirdparty/glew_fork/include']
    sources = [ROOT/'tests/csol-scenario-hud/main.cpp', ROOT/'native/csol/scenario_abilities.cpp',
        ROOT/'native/csol/client/scenario_abilities.cpp', ROOT/'native/csol/scenario_upgrades.cpp',
        ROOT/'native/csol/client/scenario_upgrades.cpp', ROOT/'native/csol/image_codec.cpp']
    inputs = [*sources, Path(__file__).resolve(), ROOT/'native/csol/scenario_abilities.hpp',
        ROOT/'native/csol/scenario_upgrades.hpp', ROOT/'native/csol/client/runtime.hpp',
        ROOT/'native/csol/client/scenario_abilities.hpp', ROOT/'native/csol/client/scenario_upgrades.hpp',
        ROOT/'native/csol/image_codec.hpp']
    before = [identity(path) for path in inputs]
    output.mkdir(parents=True, exist_ok=False)
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
        '/utf-8', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
        '/DWIN32', '/DNOMINMAX', '/DNO_MALLOC_OVERRIDE', '/DGLEW_STATIC', '/DGLEW_NO_GLU',
        *['/I'+str(path) for path in includes]]
    objects, commands, logs = [], [], []
    compile_exit = 0
    for index, source in enumerate(sources):
        obj = output / f'hud-{index}.obj'
        command = [clang, *flags, '/c', str(source), '/Fo'+str(obj)]
        commands.append(command)
        compiled = subprocess.run(command, env=env, cwd=output, capture_output=True,
            text=True, encoding='utf-8', errors='replace')
        logs.append(compiled.stdout+compiled.stderr)
        if compiled.returncode:
            compile_exit = compiled.returncode
            break
        objects.append(obj)
    executable = output / 'scenario-hud.exe'
    if not compile_exit:
        command = [clang, '--target=i686-pc-windows-msvc', '/nologo', '/clang:-flto=thin',
            '/clang:-fuse-ld=lld', *map(str, objects), '/Fe'+str(executable),
            '/link', '/MACHINE:X86', '/opt:lldltojobs=1', '/threads:1', 'gdi32.lib', 'user32.lib']
        commands.append(command)
        linked = subprocess.run(command, env=env, cwd=output, capture_output=True,
            text=True, encoding='utf-8', errors='replace')
        logs.append(linked.stdout+linked.stderr)
        compile_exit = linked.returncode
    (output/'compile.log').write_text(''.join(logs), encoding='utf-8')
    (output/'commands.json').write_text(json.dumps(commands, indent=2)+'\n', encoding='utf-8')
    test_exit, test_output = None, ''
    if not compile_exit:
        result = subprocess.run([str(executable)], env=env, cwd=output, capture_output=True,
            text=True, encoding='utf-8', errors='replace', timeout=30)
        test_exit, test_output = result.returncode, result.stdout+result.stderr
        (output/'test.log').write_text(test_output, encoding='utf-8')
    after = [identity(path) for path in inputs]
    receipt = {'atUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'inputsBefore': before, 'inputsAfter': after, 'inputsUnchanged': before == after,
        'compileExit': compile_exit, 'testExit': test_exit, 'output': test_output,
        'boundary': 'Real core and separately compiled client consumers; engine/message/time fixtures. No assets, original binaries, GPU, dedicated server or DEM playback.'}
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(test_output or ''.join(logs), end='')
    if compile_exit or test_exit or before != after:
        raise RuntimeError('Compile/test failed or sources changed; see output receipt')


if __name__ == '__main__':
    main()
