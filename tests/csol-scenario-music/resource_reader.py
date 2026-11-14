#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the production music/config reader and verify the cold download plan."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    out = sdk.checked(args.output, exists=False)
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace receipt directory')
    sources = [ROOT / 'native/csol' / path for path in (
        'scenario/core/music_resources.cpp', 'scenario/core/scenario_music.cpp',
        'scenario/core/config.cpp', 'scenario/core/round_settings.cpp',
        'scenario/core/spawn_points.cpp', 'scenario/custom/settings.cpp')]
    tracked = [sdk.identity(path) for path in [
        *sorted((ROOT / 'native/csol').rglob('*.hpp')),
        *sorted((ROOT / 'native/csol').rglob('*.h')),
        *sorted((ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include').rglob('*.h')),
        *sources, Path(__file__), ROOT / 'tools/Prepare-CSOLScenarioMusic.py',
        ROOT / 'tests/csol-scenario-map-session/fixtures.hpp',
        ROOT / 'native/csol/config/gameplay.example.json', args.environment, args.compiler]]
    before = {row['path'].casefold(): row for row in tracked}
    env = dict(os.environ)
    for key, value in sdk.read(args.environment).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    out.mkdir(parents=True)
    observed = {}
    steps = []

    def run(name, command, data=None):
        done = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                              input=data, capture_output=True, timeout=120)
        log = done.stdout + done.stderr
        (out / (name + '.log')).write_bytes(log)
        steps.append(dict(name=name, exit=done.returncode))
        for value in re.findall(r'Note: including file:\s*([^\r\n]+)', log.decode('utf8', 'replace')):
            path = sdk.checked(value.strip())
            if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / 'build'):
                row = sdk.identity(path)
                if before.get(str(path).casefold()) != row:
                    raise ValueError('Unrecorded actual resource-reader dependency: ' + str(path))
                observed[str(path).casefold()] = row
        return done

    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
             '/utf-8', '/showIncludes', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS',
             '/I' + str(ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
    objects = []
    for i, source in enumerate(sources):
        obj = out / (str(i) + '.obj')
        result = run('compile-' + str(i), [args.compiler, *flags, '/c', source, '/Fo' + str(obj)])
        if result.returncode:
            raise RuntimeError(result.stderr.decode('utf8', 'replace'))
        objects.append(obj)
    exe = out / 'music-resources.exe'
    assert not run('link', [args.compiler, '--target=i686-pc-windows-msvc', '/MT',
                           '-fuse-ld=lld', *objects, '/Fe' + str(exe), '/link', '/threads:1']).returncode
    config = json.loads((ROOT / 'native/csol/config/gameplay.example.json').read_text('utf8'))
    checks = 0
    for enabled in (False, True):
        config['features']['time_cycle']['enabled'] = enabled
        body = json.dumps(config, ensure_ascii=False).encode()
        result = run('config-' + str(enabled), [exe, 'config'], body)
        assert not result.returncode
        assert result.stdout.decode().splitlines() == (sorted(['bgm/Play_BGM_Rex.mp3','bgm/BGM_Rex_loop.mp3','bgm/Zsh_nightbgm.mp3','bgm/BGM_zombie_attack.mp3']) if enabled else [])
        checks += 2
    for i, body in enumerate((b'{}', b'{', b'{"version":99}', b'{"version":2,"features":{"time_cycle":{"enabled":1}}}')):
        result = run('invalid-' + str(i), [exe, 'config'], body)
        assert result.returncode
        checks += 1
    fixtures = (ROOT / 'tests/csol-scenario-map-session/fixtures.hpp').read_text('utf8')
    rounds = bytes.fromhex(re.search(r'\brounds="([0-9a-f]+)"', fixtures)[1])
    result = run('round', [exe, 'round'], rounds)
    assert not result.returncode and b'bgm/Scenario_Ready.mp3' in result.stdout
    assert b'bgm/Play_BGM_Rex.mp3' not in result.stdout
    checks += 2
    common, bgm = out / 'common', out / 'original-bgm'
    (common / 'maps').mkdir(parents=True)
    bgm.mkdir()
    recovery=run('recovery',[exe,'recovery'],b'')
    assert not recovery.returncode and set(recovery.stdout.decode().splitlines())=={
        'sound/vox/man/sprint_end_default.wav','sound/vox/woman/sprint_end_default.wav'}
    for name in recovery.stdout.decode().splitlines():
        path=common/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'explicit WAV existence fixture')
    checks+=1
    for path in result.stdout.decode().splitlines() + ['bgm/Play_BGM_Rex.mp3','bgm/BGM_Rex_loop.mp3','bgm/Zsh_nightbgm.mp3','bgm/BGM_zombie_attack.mp3']:
        (bgm / path[4:]).write_bytes(b'explicit fixture bytes; no audio decoding claim')
    (common / 'maps/zbs_music_fixture.cso').write_bytes(rounds)
    entity = b'{\n"classname" "worldspawn"\n}\n\0'
    header = bytearray(124)
    struct.pack_into('<iii', header, 0, 30, 124, len(entity))
    (common / 'maps/music_fixture.bsp').write_bytes(header + entity)
    config['features']['time_cycle']['enabled'] = True
    config_file = out / 'gameplay.json'
    config_file.write_text(json.dumps(config, ensure_ascii=False), 'utf8')
    prepare = run('prepare', [sys.executable, '-B', ROOT / 'tools/Prepare-CSOLScenarioMusic.py',
                             '--common', common, '--bgm', bgm, '--reader', exe,
                             '--map', 'music_fixture', '--gameplay-config', config_file,
                             '--output', out / 'staged-resources'])
    assert not prepare.returncode
    plan = sdk.read(out / 'staged-resources/resource-plan.json')
    assert any(row['destination'] == 'bgm/Play_BGM_Rex.mp3' for row in plan['files'])
    assert any(Path(row['path']) == config_file for row in plan['inputs'])
    checks += 3
    for row in before.values():
        sdk.verify(row)
    receipt = dict(success=True, inputsUnchanged=True, checks=checks, steps=steps,
                   inputs=list(before.values()), observed=list(observed.values()),
                   reader=sdk.identity(exe), deployed=False, gameplay=False,
                   boundary='Actual config/round readers and resource preparer; file bytes are explicit fixtures')
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf8')
    print(json.dumps(dict(success=True, inputsUnchanged=True, checks=checks)))


if __name__ == '__main__':
    main()
