#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the actual KILL HUD and healing consumers with bounded SDK fixtures.

Run from the companion PowerShell entry point to initialize the x86 toolchain.
These tests capture engine calls; they do not claim live rendering or gameplay.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    output = args.output.absolute()
    if not output.resolve().is_relative_to(ROOT / 'build'):
        parser.error('Output must remain under workspace build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('Linked output directory refused')
    output.mkdir(parents=True, exist_ok=False)
    metahook = ROOT / 'external/MetaHookSv-20261007'
    sdk = metahook / 'MetaHook/include'
    includes = [ROOT / 'native', ROOT / 'native/csol/client', ROOT / 'native/csol']
    includes += [sdk / path for path in ('', 'Interface', 'HLSDK/cl_dll', 'HLSDK/common',
        'HLSDK/engine', 'HLSDK/pm_shared', 'HLSDK/public', 'SourceSDK', 'SourceSDK/tier0',
        'SourceSDK/tier1', 'VGUI', 'Interface/VGUI')]
    includes += [metahook / path for path in ('MetaHook/thirdparty/capstone_fork/include',
        'MetaHook/thirdparty/rapidjson/include', 'thirdparty/glew_fork/include',
        'Plugins/Renderer/include/Interface', 'Plugins/VGUI2Extension/include/Interface')]
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
        '/W4', '/utf-8', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
        '/Gy', '/Gw', '/DNDEBUG', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS',
        '/DNO_MALLOC_OVERRIDE', '/DGLEW_STATIC', '/DGLEW_NO_GLU']
    units = {
        'kill-state': 'native/csol/scenario_kill.cpp',
        'kill-state-test': 'tests/csol-scenario-kill/main.cpp',
        'kill-client': 'native/csol/client/scenario_kill.cpp',
        'kill-client-test': 'tests/csol-scenario-kill/client.cpp',
        'image': 'native/csol/image_codec.cpp',
        'healing-client': 'native/csol/client/scenario_effects.cpp',
        'healing-client-test': 'tests/csol-scenario-monster-healing/client.cpp',
    }
    inputs = set(units.values()) | {
        'native/csol/scenario_kill.hpp', 'native/csol/scenario_healing.hpp',
        'native/csol/client/scenario_kill.hpp', 'native/csol/client/scenario_effects.hpp',
        'native/csol/client/runtime.hpp', 'native/csol/image_codec.hpp',
        'tests/csol-scenario-kill/original-expectations.hpp'}

    def hashes():
        return {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sorted(inputs)}

    before = hashes()
    commands, checks = [], {}

    def run(name, command):
        command = [str(value) for value in command]
        result = subprocess.run(command, cwd=output, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=180)
        log = result.stdout + result.stderr
        (output / (name + '.log')).write_text(log, encoding='utf-8')
        commands.append(dict(name=name, command=command, exitCode=result.returncode))
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        if result.returncode:
            raise RuntimeError(name + ' failed; see ' + str(output / (name + '.log')))
        return log

    for name, path in units.items():
        run(name + '-compile', [args.clang, *flags, *['/I' + str(path) for path in includes],
            '/c', ROOT / path, '/Fo' + str(output / (name + '.obj'))])
    for name, members in (
        ('kill-state', ('kill-state', 'kill-state-test')),
        ('kill-client', ('kill-state', 'kill-client', 'kill-client-test', 'image')),
        ('healing-client', ('healing-client', 'healing-client-test'))):
        run(name + '-link', [args.clang, '--target=i686-pc-windows-msvc', '/MT',
            '/clang:-fuse-ld=lld', *[output / (member + '.obj') for member in members],
            '/Fe' + str(output / (name + '.exe')), '/link', '/OPT:REF', '/OPT:ICF',
            '/OPT:LLDLTO=3', '/threads:1'])
        for arguments in ([[]] if name == 'kill-state' else [[], ['missing']]):
            case = name + ('-missing-handler' if arguments else '')
            log = run(case + '-run', [output / (name + '.exe'), *arguments])
            match = re.search(r'(\d+) checks(?: passed)?', log)
            if not match:
                raise RuntimeError('Missing fixture result: ' + case)
            checks[case] = int(match[1])
            print(log.strip())
    if hashes() != before:
        raise RuntimeError('Production or fixture inputs changed during verification')
    receipt = dict(at=datetime.now(timezone.utc).isoformat(), inputs=before, checks=checks,
        actualProductionSource=True, maxParallelCompiles=1, deployed=False, liveGameplay=False,
        boundaries=['SDK engine calls and resource bytes are controlled fixtures.',
                    'Healing target selection/server damage and actual GL output are not covered.'])
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
