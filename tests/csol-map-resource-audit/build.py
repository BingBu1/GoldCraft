#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build only the read-only production map inventory and dependency collector."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--reuse', type=Path)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True,
                        help='JSON object containing the x86 compiler environment, including INCLUDE and LIB')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('fresh workspace build directory required')
    for p in (out, *out.parents):
        if p.is_symlink() or p.is_junction():
            raise ValueError('linked output')
    out.mkdir(parents=True)
    compiler = args.compiler.resolve(strict=True)
    if identity(compiler)['sha256'] != 'dd5986f7acdcece0329ed801862b8fd81cd3afc69e21f21b03d5fd9ba9c02474':
        raise ValueError('compiler identity changed')
    env = dict(os.environ)
    env.update(json.loads(args.environment.read_text('utf-8-sig')))
    env['VSLANG'] = '1033'
    scenario = ['core/map_resources', 'core/map_catalog', 'core/config', 'core/resources', 'core/waves',
                'core/trigger_rules', 'core/chaos_routes', 'core/spawn_points', 'core/upgrade_tables',
                'core/difficulty', 'core/round_settings', 'core/scenario_music', 'core/message_table',
                'core/message_resource', 'core/dialogue_resources', 'core/wave_events',
                'monsters/monster_types', 'monsters/monster_appearances', 'monsters/champions',
                'monsters/difficulty_monsters', 'bosses/boss_skills']
    catalog = [ROOT / ('native/csol/scenario/' + s + '.cpp') for s in scenario]
    catalog += [Path(__file__).with_name('main.cpp')]
    collector = [ROOT / ('native/csol/' + s + '.cpp') for s in
                 ['resource_list', 'dependencies', 'model_codec', 'model_json', 'events', 'model_source', 'sequences', 'texture_model']]
    catalog += [p for p in collector if p.stem != 'resource_list']
    sources = sorted(set(catalog + collector))
    watch = {p.resolve(): identity(p) for p in sources}
    for base in [ROOT / 'native/csol', ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include']:
        for p in base.rglob('*'):
            if p.is_file() and p.suffix in ('.h', '.hpp', '.inc'):
                watch[p.resolve()] = identity(p)
    for base in [Path(p) for p in env['INCLUDE'].split(';') if p]:
        if base.exists():
            for p in base.rglob('*'):
                if p.is_file(): watch[p.resolve()] = identity(p)
    libraries = {}
    for base in [Path(p) for p in env['LIB'].split(';') if p]:
        if base.exists():
            for p in base.glob('*.lib'): libraries[p.resolve()] = identity(p)
    def save(name, value):
        (out / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf-8')
    flags = [str(compiler), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT', '/utf-8',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/Gy', '/Gw', '/fp:precise', '/Y-',
             '/showIncludes', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS',
             '/I' + str(ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
    build_signature = dict(compiler=identity(compiler), flags=flags,
        environmentSha256=hashlib.sha256(json.dumps(env, sort_keys=True,
            ensure_ascii=False).encode('utf-8')).hexdigest())
    commands = []
    objects = {}
    reused = []
    previous = args.reuse.resolve() if args.reuse else None
    if previous:
        old_plan=json.loads((previous/'plan.json').read_text('utf-8'))
        old_result=json.loads((previous/'result.json').read_text('utf-8'))
        if not old_result['success']:raise ValueError('reuse requires successful prior build')
        if old_plan.get('buildSignature') != build_signature:
            raise ValueError('reuse requires identical compiler, flags and environment completion inputs')
        anchored={item['path']:item for item in old_result.get('objects',[])}
        for item in old_plan['sources']:
            if Path(item['path']) != Path(__file__).with_name('main.cpp').resolve() and identity(item['path']) != item:
                raise ValueError('reuse input changed: '+item['path'])
    for i, p in enumerate(sources):
        objects[p] = out / f'{i:02}-{p.stem}.obj'
        if previous and p != Path(__file__).with_name('main.cpp'):
            objects[p] = previous / f'{i:02}-{p.stem}.obj'
            item=identity(objects[p])
            if anchored.get(item['path']) != item:raise ValueError('reused object lacks matching completion identity')
            reused.append(item);continue
        commands.append(dict(name=f'compile-{i:02}-{p.stem}', argv=[*flags, '/c', str(p), '/Fo' + str(objects[p])]))
    for name, files in [('catalog', catalog), ('collector', collector)]:
        commands.append(dict(name='link-' + name, argv=[str(compiler), '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
            *[str(objects[p]) for p in files], '/Fe' + str(out / (name + '.exe')), '/link', '/OPT:REF', '/OPT:ICF', '/threads:1', '/verbose']))
    save('plan.json', dict(commands=commands, sources=list(watch.values()), libraries=list(libraries.values()),
        compiler=identity(compiler), buildSignature=build_signature, reused=reused,
        head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(), copiedSources=False))
    result = dict(success=False, commands=[], deployment=False)
    try:
        for command in commands:
            t = time.monotonic()
            ran = subprocess.run(command['argv'], env=env, cwd=out, capture_output=True, timeout=180, creationflags=subprocess.CREATE_NO_WINDOW)
            log = out / (command['name'] + '.log'); log.write_bytes(ran.stdout + ran.stderr)
            result['commands'].append(dict(name=command['name'], exit=ran.returncode, seconds=time.monotonic()-t, log=identity(log)))
            print(command['name'], ran.returncode, flush=True)
            if ran.returncode: raise RuntimeError('build failed: ' + command['name'])
        result['success'] = True
        result['products'] = [identity(out / (n + '.exe')) for n in ('catalog', 'collector')]
        result['objects'] = [identity(p) for p in objects.values()]
    finally:
        changed = [str(p) for p, value in {**watch, **libraries}.items() if identity(p) != value]
        result['changedInputs'] = changed
        result['changedReusedObjects'] = [item['path'] for item in reused if identity(item['path']) != item]
        result['success'] &= not changed and not result['changedReusedObjects']
        save('result.json', result)
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
