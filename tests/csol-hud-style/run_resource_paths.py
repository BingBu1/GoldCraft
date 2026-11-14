#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare deployment preflight with actual C++ HUD resource consumers."""
import argparse
import cProfile
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import csol_hud_resources as hud


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('client-build', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--resources', type=Path, action='append', required=True)
    args = parser.parse_args()
    out, build = args.output.resolve(), args.client_build.resolve(strict=True)
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace receipt directory')
    helper = load('registry_test_helpers', ROOT / 'tests/csol-weapon-modules/run_dll.py')
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    cache = (build / 'CMakeCache.txt').read_text('utf-8-sig')
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache, re.M)[1].strip()
    database = json.loads(subprocess.check_output([ninja, '-C', str(build), '-t', 'compdb'], env=env))
    row = next(row for row in database if row['file'].replace('\\', '/').endswith('/native/csol/hud_style.cpp'))
    original, flags, skip = helper.split(row['command']), [], False
    for value in original[1:]:
        if skip:
            skip = False
            continue
        if value.lower() in ('-o', '-mf', '-mt'):
            skip = True
            continue
        if value in ('/c', '-c', '-MD', '--') or value.lower().endswith('.cpp') or value.lower().startswith(('/fo', '/fd')):
            continue
        flags.append(value)
    constants, images = hud.contract()
    paths = [ROOT / 'native/csol/hud_style.cpp', ROOT / 'native/csol/hud_style.hpp',
             ROOT / 'native/csol/scenario/custom/phase_art.hpp',
             ROOT / 'tests/csol-hud-style/resource_paths.cpp', ROOT / 'tools/csol_hud_resources.py',
             ROOT / 'tests/csol-weapon-modules/run_dll.py', Path(__file__), args.environment,
             Path(original[0]), build / 'CMakeCache.txt', build / 'build.ninja',
             *(root / constants['CatalogPath'] for root in args.resources)]
    paths.extend(path for root in args.resources if (path := root / 'addons/goldcraft/config/gameplay.json').is_file())
    before = [helper.identity(path) for path in paths]
    out.mkdir(parents=True)
    steps, checks, dependencies, success = [], 0, {}, False

    def run(name, command):
        done = subprocess.run(list(map(str, command)), cwd=build, env=env, capture_output=True, timeout=120)
        text = (done.stdout + done.stderr).decode('utf8', 'replace')
        (out / (name + '.log')).write_text(text, 'utf8')
        for path in re.findall(r'Note: including file:\s*([^\r\n]+)', text):
            path = Path(path.strip()).resolve(strict=True)
            dependencies[str(path)] = helper.identity(path)
        steps.append(dict(name=name, command=list(map(str, command)), exit=done.returncode))
        if done.returncode:
            raise RuntimeError(name + ': ' + text[-2000:])
        return text

    try:
        objects = []
        for i, source in enumerate((paths[0], ROOT / 'tests/csol-hud-style/resource_paths.cpp')):
            obj = out / f'{i}.obj'
            run(f'compile-{i}', [original[0], *flags, '/showIncludes', '/c', '/Fo' + str(obj), source])
            objects.append(obj)
        exe = out / 'resource-paths.exe'
        run('link', [original[0], '/nologo', '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
                     *objects, '/Fe' + str(exe), '/link', '/machine:x86', '/threads:1', '/OPT:REF'])
        profiles, results = cProfile.Profile(), []
        for i, root in enumerate(args.resources):
            catalog = root / constants['CatalogPath']
            actual = run(f'cpp-resources-{i}', [exe, catalog.resolve()]).splitlines()
            expected = hud.dependencies(catalog.read_text('utf-8-sig'), constants, images)
            assert actual == expected
            checks += 1
            result = profiles.runcall(hud.verify, root)
            assert result['success']
            results.append(result)
            checks += 1
        profiles.dump_stats(out / 'preflight-cpu.pstats')
        fixture = out / 'fixture'
        source_catalog = args.resources[0] / constants['CatalogPath']
        names = hud.dependencies(source_catalog.read_text('utf-8-sig'), constants, images)
        omitted = {'resource/csol/hud/v1/hud_weapon_on_bg.tga', 'resource/csol/hud/v1/weapon_list_new.tga',
                   'resource/csol/hud/v1/weapon_selection_new.tga'}
        for name in names:
            path = fixture / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if name == constants['CatalogPath']:
                path.write_bytes(source_catalog.read_bytes())
            elif name not in omitted:
                path.write_bytes(b'\0' * 12 + struct.pack('<HH', *images[name]) + b'\x20\x08' if name in images else b'fixture')
        assert set(hud.verify(fixture)['missing']) == omitted
        checks += 1
        for name in omitted:
            (fixture / name).write_bytes(b'\0' * 12 + struct.pack('<HH', *images[name]) + b'\x20\x08')
        assert hud.verify(fixture)['success']
        checks += 1
        bad = 'resource/csol/hud/v1/money_bg.tga'
        (fixture / bad).write_bytes(b'\0' * 12 + struct.pack('<HH', 24, 24) + b'\x20\x08')
        assert hud.verify(fixture)['invalidDimensions'] == [bad]
        checks += 1
        duplicated = '{"version":1,' + source_catalog.read_text('utf-8-sig').lstrip()[1:]
        try:
            hud.dependencies(duplicated, constants, images)
        except ValueError:
            checks += 1
        else:
            raise AssertionError('Ambiguous duplicate HUD members must fail preflight')
        (fixture / bad).write_bytes(b'\0' * 12 + struct.pack('<HH', *images[bad]) + b'\x20\x08')
        config = fixture / 'addons/goldcraft/config/gameplay.json'
        config.parent.mkdir(parents=True)
        config.write_text('{"features":{"time_cycle":{"enabled":true}}}', 'utf8')
        animated = hud.verify(fixture)
        assert set(animated['missing']) == {'resource/goldcraft/challenge/night_blood_v1.tga'}
        checks += 1
        config.write_text('{"features":{"time_cycle":{"enabled":false}}}', 'utf8')
        assert hud.verify(fixture)['success']
        checks += 1
        success = True
    finally:
        stable = before == [helper.identity(path) for path in paths] and all(helper.identity(path) == row for path, row in dependencies.items())
        receipt = dict(success=success and stable, inputsUnchanged=stable, checks=checks, inputs=before,
                       dependencies=list(dependencies.values()), steps=steps, results=results if success else [],
                       boundary='Actual C++ ParseCatalog/ResourcePaths and real server preflight; negative assets are generated fixture bytes, no client rendering')
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    if not receipt['success']:
        raise SystemExit(1)
    print(json.dumps(dict(success=True, checks=checks, inputsUnchanged=stable)))


if __name__ == '__main__':
    main()
