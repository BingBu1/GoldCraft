#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual scenario music consumers against bounded engine fixtures.

Requires the configured x86 Clang/MSVC environment; the companion PowerShell
entry point initializes it. No game process, audio device or server is used.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / 'tests/csol-scenario-music'


def identity(path):
    return {'path': path.relative_to(ROOT).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--countdown-only', action='store_true')
    selection.add_argument('--client-only', action='store_true')
    args = parser.parse_args()
    output = args.output.absolute()
    if not output.resolve().is_relative_to(ROOT / 'build'):
        parser.error('Output must remain under workspace build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('Linked output directory refused')
    output.mkdir(parents=True, exist_ok=False)

    inputs = [ROOT / name for name in (
        'native/csol/scenario_music.hpp', 'native/csol/scenario_music.cpp',
        'native/csol/client/runtime.hpp', 'native/csol/client/scenario_music.hpp',
        'native/csol/client/scenario_music.cpp', 'native/csol/server/scenario_music.h',
        'native/csol/server/scenario_music.cpp', 'native/csol/server/scenario_bgm_trigger.h',
        'native/csol/server/scenario_bgm_trigger.cpp',
        'native/csol/server/scenario_entity_access.h',
        'native/csol/scenario/round_settings.cpp', 'native/csol/scenario/round_settings.hpp',
        'native/csol/scenario/map_resources.hpp', 'native/csol/scenario/config.hpp',
        'native/csol/scenario/resources.hpp', 'native/csol/scenario/monster_types.hpp',
        'native/csol/scenario/trigger_rules.hpp')]
    if args.client_only:
        inputs = [ROOT / name for name in (
            'native/csol/scenario_music.hpp', 'native/csol/scenario_music.cpp',
            'native/csol/client/runtime.hpp', 'native/csol/client/scenario_music.hpp',
            'native/csol/client/scenario_music.cpp',
            'external/MetaHookSv-20261007/MetaHook/src/metahook.cpp')]
        inputs += [TESTS / 'client.cpp', TESTS / 'README.md']
    else:
        inputs += sorted(path for path in TESTS.rglob('*') if path.is_file())
    inputs += [Path(__file__), ROOT / 'tools/Test-CSOLScenarioMusic.ps1']
    before = [identity(path) for path in inputs]
    extracted = []
    for filename, signature, target in (
        ('scenario_music.cpp', 'bool CCSOLScenarioMusic::PrecacheFile', 'actual_server.inc'),
        ('scenario_bgm_trigger.cpp', 'void CCSOLTriggerBGM::KeyValue', 'actual_trigger.inc')):
        if args.client_only:
            continue
        source = ROOT / 'native/csol/server' / filename
        text = source.read_text(encoding='utf-8')
        if text.count(signature) != 1:
            raise RuntimeError('Expected unique production method boundary: ' + signature)
        body = text[text.index(signature):]
        (output / target).write_text(body, encoding='utf-8', newline='\n')
        extracted.append(dict(source=identity(source),
                              extractedSha256=hashlib.sha256(body.encode()).hexdigest()))

    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
             '/W4', '/utf-8', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2',
             '/fp:precise', '/Gy', '/Gw', '/DNDEBUG', '/DNOMINMAX',
             '/D_CRT_SECURE_NO_WARNINGS', '/DNO_MALLOC_OVERRIDE',
             '/DGLEW_STATIC', '/DGLEW_NO_GLU']
    includes = [ROOT / 'native', ROOT / 'native/csol/client',
                ROOT / 'native/csol/server', ROOT / 'native/csol', output]
    metahook = ROOT / 'external/MetaHookSv-20261007'
    sdk = metahook / 'MetaHook/include'
    client_includes = [sdk / name for name in ('', 'Interface', 'HLSDK/cl_dll',
        'HLSDK/common', 'HLSDK/engine', 'HLSDK/pm_shared', 'HLSDK/public',
        'SourceSDK', 'SourceSDK/tier0', 'SourceSDK/tier1')]
    client_includes += [metahook / name for name in (
        'MetaHook/thirdparty/capstone_fork/include',
        'MetaHook/thirdparty/rapidjson/include', 'thirdparty/glew_fork/include',
        'Plugins/Renderer/include/Interface')]
    commands, results = [], []

    def run(name, arguments):
        command = [str(value) for value in arguments]
        commands.append(dict(name=name, command=command))
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        result = subprocess.run(command, cwd=output, capture_output=True, text=True,
                                encoding='utf-8', errors='replace')
        log = result.stdout + result.stderr
        (output / (name + '.log')).write_text(log, encoding='utf-8')
        results.append(dict(name=name, exitCode=result.returncode))
        (output / 'exit-codes.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
        if result.returncode:
            raise RuntimeError(name + ' failed; see ' + str(output / (name + '.log')))
        return log

    objects = {}
    units = (
        ('protocol', ROOT / 'native/csol/scenario_music.cpp', []),
        ('countdown-test', TESTS / 'countdown.cpp', []),
        ('client', ROOT / 'native/csol/client/scenario_music.cpp', client_includes),
        ('client-test', TESTS / 'client.cpp', client_includes),
        ('round-settings', ROOT / 'native/csol/scenario/round_settings.cpp', []),
        ('server-test', TESTS / 'server.cpp', []),
        ('trigger-test', TESTS / 'trigger.cpp', [TESTS / 'stubs']))
    for name, source, extra in units:
        if args.countdown_only and name not in ('protocol', 'countdown-test'):
            continue
        if args.client_only and name not in ('protocol', 'client', 'client-test'):
            continue
        obj = output / (name + '.obj')
        run(name + '-compile', [args.clang, *flags,
            *['/I' + str(path) for path in extra + includes], '/c', source, '/Fo' + str(obj)])
        objects[name] = obj
    for name, members in (
        ('countdown', ('protocol', 'countdown-test')),
        ('client', ('protocol', 'client', 'client-test')),
        ('server', ('protocol', 'round-settings', 'server-test')),
        ('trigger', ('protocol', 'trigger-test'))):
        if args.countdown_only and name != 'countdown':
            continue
        if args.client_only and name != 'client':
            continue
        run(name + '-link', [args.clang, '--target=i686-pc-windows-msvc', '/MT',
            '/clang:-fuse-ld=lld', *[objects[member] for member in members],
            '/Fe' + str(output / (name + '.exe')), '/link', '/OPT:REF', '/OPT:ICF',
            '/OPT:LLDLTO=3', '/threads:1'])
    checks = {}
    for name, command in (
        ('countdown', [output / 'countdown.exe']),
        ('client', [output / 'client.exe']),
        ('client-no-previous', [output / 'client.exe', '--no-previous']),
        ('client-registered-null', [output / 'client.exe', '--registered-null']),
        ('server', [output / 'server.exe']), ('trigger', [output / 'trigger.exe'])):
        if args.countdown_only and name != 'countdown':
            continue
        if args.client_only and not name.startswith('client'):
            continue
        log = run(name + '-run', command)
        match = re.search(r'PASS BGM actual .*? (\d+) checks', log)
        if not match:
            raise RuntimeError('Missing fixture result for ' + name)
        checks[name] = int(match[1])
        print(log.strip())
    after = [identity(path) for path in inputs]
    if after != before:
        raise RuntimeError('Production or fixture source changed during testing')
    receipt = dict(at=datetime.now(timezone.utc).isoformat(), inputs=before,
        extractedMethods=extracted, checks=checks, inputHashesUnchanged=True,
        actualProductionSource=True, countdownOnly=args.countdown_only,
        clientOnly=args.client_only,
        parallelCompiles=1, audioDevice=False,
        deployed=False, liveGameplay=False,
        boundaries=['Current() authority, message delivery and engine entity operations are fixtures.',
                    ('Server and trigger methods are not extracted or compiled in client-only mode.'
                     if args.client_only else
                     'Server and trigger method bodies are extracted unchanged from production source.'),
                    'The client uses actual SDK types and real production decoder/consumer.',
                    'MetaHook only replaces registered message nodes; the engine registration fixture installs missing nodes.',
                    'Client command strings are captured, not executed by the engine or an audio device.',
                    'Foreign MP3 commands are outside module ownership tracking.'])
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
