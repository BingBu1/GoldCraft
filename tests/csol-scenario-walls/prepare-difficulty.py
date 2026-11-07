#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze a current-SDK wall difficulty test; execution is a separate action."""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import os
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SDK_REL = Path('external/ReGameDLL_CS/regamedll')
OLD = ROOT / 'build/csol-scenario-walls-tests-20261026'
SDK_DIRS = ['', 'hookers', 'common', 'dlls', 'engine', 'public',
            'public/regamedll', 'pm_shared', 'regamedll', 'testsuite']


def identity(path):
    path = Path(path).resolve()
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def save(path, value):
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)


def no_links(path):
    for ancestor in (path, *path.parents):
        assert not ancestor.is_symlink() and not ancestor.is_junction(), ancestor


def function(raw, signature):
    start = raw.index(signature)
    opening = raw.index(b'{', start)
    depth = 0
    for end in range(opening, len(raw)):
        depth += (raw[end] == 123) - (raw[end] == 125)
        if depth == 0:
            return start, end + 1
    raise ValueError('unbalanced production function')


def prepare(out, suite='difficulty', common=None):
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    no_links(out)
    out.mkdir(parents=True)
    candidate = out / 'source'
    copies, slices, input_rows = [], [], {}

    def register(path):
        row = identity(path)
        input_rows[os.path.normcase(row['path'])] = row
        return row

    def write(relative, data):
        target = candidate / relative
        no_links(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(data)
        assert target.stat().st_nlink == 1
        register(target)
        return target

    def copy(relative):
        source = ROOT / relative
        no_links(source)
        before = register(source)
        target = write(relative, source.read_bytes())
        after = identity(source)
        assert before == after and identity(target)['sha256'] == before['sha256']
        copies.append(dict(origin=before, frozen=identity(target), independent=True))
        return target

    def extract(name, selections, extra=''):
        chunks = [b'#include "precompiled.h"\n', extra.encode()]
        regions = []
        for relative, start, finish in selections:
            source = ROOT / SDK_REL / relative
            register(source)
            raw = source.read_bytes()
            a, b = function(raw, start) if finish is None else (raw.index(start), raw.index(finish, raw.index(start)))
            body = raw[a:b]
            chunks += [body, b'\n']
            regions.append(dict(origin=identity(source), byteStart=a, byteEndExclusive=b,
                                sliceSHA256=hashlib.sha256(body).hexdigest()))
        target = write(SDK_REL / 'dlls' / name, b'\n'.join(chunks))
        slices.append(dict(output=identity(target), regions=regions))
        return target

    # Freeze full production units and all test support anew. No old object or
    # old-layout SDK header is carried into the candidate.
    sources = []
    for name in ('scenario_breakable', 'scenario_breakable_guard', 'scenario_wall_rewards'):
        sources.append(copy(Path('native/csol/server') / (name + '.cpp')))
    for name in ('difficulty', 'wall_damage'):
        sources.append(copy(Path('native/csol/scenario') / (name + '.cpp')))
    test_names = ('fixture.cpp', 'sdk-boundary.cpp', 'difficulty-boundary.cpp') + (
        ('numeric.cpp', 'gate-main.cpp') if suite == '2gate' else ('difficulty-main.cpp',))
    for name in test_names:
        sources.append(copy(Path('tests/csol-scenario-walls') / name))
    if suite == '2gate':
        if common is None:
            raise ValueError('--common is required for the actual zs_2gate wall suite')
        import importlib.util
        generator = ROOT / 'tests/csol-scenario-2gate/prepare.py'
        register(generator)
        spec = importlib.util.spec_from_file_location('gate_entities', generator)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        bsp = common / 'maps/zs_2gate.bsp'
        register(bsp)
        raw = bsp.read_bytes()
        if hashlib.sha256(raw).hexdigest() != module.BSP_SHA256:
            raise ValueError('Unexpected original zs_2gate BSP identity')
        entities, _ = module.entities(raw)
        walls = [row for row in entities if row.get('classname') == 'zbsbreak']
        assert len(walls) == 27 and all(row.get('hmdmgratio') == '0' for row in walls)
        generated = ['struct GateWall { const char* human; const char* monster; float health; int material; };',
                     'constexpr GateWall GateWalls[] = {']
        for row in walls:
            assert not row.get('target') and not row.get('master') and not row.get('spawnflags')
            assert row.get('spawnobject') == '0' and row.get('explodemagnitude') == '0'
            generated.append('{%s,%s,%sf,%s},' % (json.dumps(row['hmdmgratio']), json.dumps(row['zbdmgratio']),
                str(float(row['health'])), row['material']))
        generated.append('};')
        write(Path('tests/csol-scenario-walls/actual_2gate_walls.inc'), ('\n'.join(generated)+'\n').encode())
        for name in ('config', 'resources', 'spawn_points', 'boss_skills',
                     'monster_types', 'trigger_rules', 'upgrade_tables'):
            sources.append(copy(Path('native/csol/scenario') / (name + '.cpp')))
        for relative in ('maps/zbs_zs_2gate.cso', 'maps/ZBS_Difficulty_Default.cso'):
            origin = common / relative
            register(origin)
            # Preserve immutable test input inside the fixture; no runtime resources are changed.
            write(Path('resources') / relative, origin.read_bytes())
    sources.append(copy(Path('tests/csol-scenario-entities/engine_stubs.cpp')))
    sources.append(copy(SDK_REL / 'regamedll/hookchains_impl.cpp'))

    # Keep the actual player hierarchy, but fail on unrelated virtual calls.
    player_source = ROOT / 'tests/csol-scenario-player-scan/player_stubs.cpp'
    register(player_source)
    lines = player_source.read_bytes().splitlines(keepends=True)
    pattern = rb'(void CBaseDelay::KeyValue|int CBaseDelay::Save|int CBaseDelay::Restore|int CBasePlayer::Classify)\('
    omitted = [line for line in lines if re.match(pattern, line)]
    assert len(omitted) == 4
    sources.append(write(Path('tests/csol-scenario-walls/player-vtable-boundary.cpp'),
                         b''.join(line for line in lines if line not in omitted)))
    guard = '#include "../../../../native/csol/server/scenario/core/scenario_breakable_guard.h"\n'
    gate = '#include "../../../../native/csol/server/scenario/core/game_rules_gate.h"\n'
    sources.append(extract('wall-difficulty-breakable.cpp', [
        ('dlls/func_break.cpp', b'// Just add more items', b'TYPEDESCRIPTION CPushable::m_SaveData[]')],
        guard + '#include "../../../../native/csol/server/scenario/core/scenario_grenade_blockers.h"\n'))
    sources.append(extract('wall-difficulty-targets.cpp', [
        ('dlls/subs.cpp', b'// This updates global tables', b'// QuakeEd only writes'),
        ('dlls/subs.cpp', b'void CBaseDelay::DelayThink()', None)], guard + '#include <string>\n'))
    sources.append(extract('wall-difficulty-relay.cpp', [
        ('dlls/triggers.cpp', b'TYPEDESCRIPTION CTriggerRelay::m_SaveData[]', b'// Global Savedata for multi_manager'),
        ('dlls/triggers.cpp', b'LINK_ENTITY_TO_CLASS(env_render', b'LINK_ENTITY_TO_CLASS(trigger, CBaseTrigger')], guard))
    service_signatures = (b'int UTIL_EntitiesInBox(', b'bool UTIL_IsMasterTriggered(',
        b'void UTIL_Remove(', b'BOOL UTIL_IsValidEntity(', b'void UTIL_MakeVectors(',
        b'void MAKE_STRING_CLASS(', b'void UTIL_ResetEntities(')
    service_methods = [('dlls/util.cpp', name, None) for name in service_signatures]
    service_methods += [('dlls/cbase.cpp', b'bool CBaseEntity::Intersects(const Vector &mins, const Vector &maxs)', None),
                        ('dlls/cbase.cpp', b'void REMOVE_ENTITY(edict_t *pEntity)', None),
                        ('dlls/bmodels.cpp', b'Vector VecBModelOrigin(', None)]
    sources.append(extract('wall-difficulty-services.cpp', service_methods,
        guard + gate + '#include "../../../../native/server/bridge.h"\n'
        '#include "../../../../native/csol/server/scenario/core/scenario_drops.h"\n'))
    account = ROOT / SDK_REL / 'dlls/player.cpp'
    raw = account.read_bytes()
    account_start = raw.rfind(b'#ifdef REGAMEDLL_API', 0, raw.index(b'void CBasePlayer::AddAccount('))
    assert account_start >= 0
    start_marker = raw[account_start:raw.index(b'void CBasePlayer::AddAccount(')] + b'void CBasePlayer::AddAccount('
    sources.append(extract('wall-difficulty-account.cpp', [
        ('dlls/player.cpp', start_marker, b'void CBasePlayer::ResetMenu()'),
        ('dlls/player.cpp', b'LINK_HOOK_CLASS_CHAIN2(int, CBasePlayer, Classify)', b'LINK_HOOK_CLASS_VOID_CHAIN(CBasePlayer, AddPoints')],
        guard + gate + '#include "../../../../native/csol/server/scenario/core/scenario_money.h"\n'
        '#include "../../../../native/csol/server/scenario/core/scenario_entity_access.h"\n'))
    api = ROOT / SDK_REL / 'dlls/API/CAPI_Impl.cpp'
    register(api)
    raw = api.read_bytes()
    a = raw.index(b'GAMEHOOK_REGISTRY(')
    b = raw.index(b';', raw.rindex(b'GAMEHOOK_REGISTRY(')) + 1
    body = raw[a:b]
    names = re.findall(rb'^GAMEHOOK_REGISTRY\((\w+)\);', body, re.M)
    assert len(names) >= 150 and len(names) == len(set(names))
    assert not re.sub(rb'GAMEHOOK_REGISTRY\(\w+\);|\s+', b'', body)
    accessors = write(SDK_REL / 'dlls/wall-difficulty-hookchain-accessors.cpp', b'#include "precompiled.h"\n' + body + b'\n')
    slices.append(dict(output=identity(accessors), regions=[dict(origin=identity(api), byteStart=a,
        byteEndExclusive=b, sliceSHA256=hashlib.sha256(body).hexdigest())], registryCount=len(names)))
    sources.append(accessors)

    environment_path = OLD / 'compiler-environment.json'
    environment = read(environment_path)
    register(environment_path)
    compiler = Path(environment['compilerRoot']) / 'bin/clang-cl.exe'
    linker = compiler.with_name('lld-link.exe')
    register(compiler); register(linker); register(Path(__file__))
    sdk_dirs = [candidate / SDK_REL / directory for directory in SDK_DIRS]
    builtin_dirs = list((compiler.parent.parent / 'lib/clang').glob('*/include'))
    assert len(builtin_dirs) == 1, 'select the actual Clang resource directory before preparing'
    # clang-cl searches its resource headers before the MSVC INCLUDE roots.
    # The wrappers then use include_next to reach the matching CRT header.
    host_dirs = builtin_dirs + [Path(p) for p in environment['INCLUDE'].split(';') if p]
    # Conservative literal-include closure. Inactive platform includes may be
    # inventoried too. The compiler's actual /showIncludes is checked later.
    queue = list(sources)
    visited, unresolved = set(), set()
    while queue:
        path = queue.pop()
        key = os.path.normcase(str(path.resolve()))
        if key in visited:
            continue
        visited.add(key)
        text = path.read_text('utf-8-sig', errors='replace')
        for next_include, delim, token in re.findall(r'^\s*#\s*include(_next)?\s*([<"])([^>"\r\n]+)[>"]', text, re.M):
            search_dirs = sdk_dirs + [candidate] + host_dirs
            if next_include:
                # Skip the containing search root, rather than resolving the
                # wrapper to itself and silently omitting the CRT dependency.
                owners = [i for i, directory in enumerate(search_dirs) if path.is_relative_to(directory)]
                assert owners, 'include_next has no known owning search root: ' + str(path)
                search_dirs = search_dirs[max(owners) + 1:]
            possible = ([path.parent / token] if delim == '"' and not next_include else []) + [p / token for p in search_dirs]
            found = None
            for at in possible:
                at = Path(os.path.abspath(at))
                if at.is_file():
                    found = at
                    break
                if at.is_relative_to(candidate):
                    origin = ROOT / at.relative_to(candidate)
                    if origin.is_file():
                        found = copy(at.relative_to(candidate))
                        break
            if found is None:
                unresolved.add(token)
                continue
            register(found)
            queue.append(found)
    library_contract = OLD / 'library-contract.json'
    register(library_contract)
    libraries = read(library_contract)['libraries']
    for library in libraries:
        register(library['resolvedByFrozenSearchOrder']['path'])

    # Capture real Gate/Session/cleanup ordering as review evidence only. It
    # is not compiled into a fake replacement for the full Gate transaction.
    ordering = [register(ROOT / p) for p in ('native/csol/server/scenario/core/game_rules_gate.cpp',
        'native/csol/server/scenario/core/scenario_session.cpp', 'external/ReGameDLL_CS/regamedll/dlls/multiplay_gamerules.cpp')]
    flags = ['/c', '/nologo', '/W4', '/utf-8', '/EHsc', '/MT', '/Gy', '/Gw', '/std:c++20',
        '--target=i686-pc-windows-msvc', '/arch:AVX2', '/O2', '/clang:-O3', '/clang:-flto=thin',
        '/fp:precise', '/showIncludes', '/Zi']
    defines = ('REGAMEDLL_ADD', 'REGAMEDLL_API', 'REGAMEDLL_FIXES', 'REGAMEDLL_SSE', 'REGAMEDLL_SELF',
        'REGAMEDLL_CHECKS', 'UNICODE_FIXES', 'BUILD_LATEST', 'BUILD_LATEST_FIXES', 'CLIENT_WEAPONS',
        'USE_QSTRING', '_CRT_SECURE_NO_WARNINGS', 'NDEBUG', 'WIN32', '_WINDOWS')
    flags += ['/D' + value for value in defines] + ['/I' + str(p) for p in sdk_dirs]
    objects, commands = [], []
    (out / 'objects').mkdir()
    for index, source in enumerate(sources):
        obj = out / 'objects' / (f'{index:02}-' + source.stem + '.obj')
        objects.append(str(obj))
        commands.append(dict(name=f'compile-{index:02}-{source.stem}', kind='compile', timeout=120,
            argv=[str(compiler), *flags, '/Fo' + str(obj), '/Fd' + str(obj.with_suffix('.pdb')), str(source)]))
    exe = out / ('wall-2gate.exe' if suite == '2gate' else 'wall-difficulty.exe')
    commands.append(dict(name='link', kind='link', timeout=180, argv=[str(linker), '/nologo', '/machine:X86',
        '/subsystem:console', '/incremental:no', '/debug', '/OPT:REF', '/OPT:ICF', '/OPT:LLDLTO=3',
        '/threads:1', '/opt:lldltojobs=1', '/verbose', '/out:' + str(exe), '/pdb:' + str(exe.with_suffix('.pdb')),
        *[row['resolvedByFrozenSearchOrder']['path'] for row in libraries if row['explicitArgument']], *objects]))
    if suite == '2gate':
        for group in ('numeric', 'metadata', '2gate', 'nonpositive'):
            commands.append(dict(name=group, kind='test', timeout=30,
                argv=[str(exe), group, str(candidate / 'resources')]))
    else:
        commands.append(dict(name='round-difficulty', kind='test', timeout=30, argv=[str(exe)]))
    # Every local source is an independent snapshot. Host SDK/toolchain files
    # are read-only identity inputs, never hard-linked or copied back.
    save(out / 'inputs.json', list(input_rows.values()))
    save(out / 'plan.json', dict(inputs=identity(out / 'inputs.json'), environment=environment,
        commands=commands, copies=copies, extractions=slices, orderingReviewInputs=ordering,
        sourceCount=len(sources), reusedObjects=0, maxJobs=1, maxSeconds=600,
        builtinIncludeRoots=[str(p) for p in builtin_dirs], includeNextHandled=True,
        literalUnresolvedIncludes=sorted(unresolved), compiled=False, executed=False,
        scope=('Actual current SDK and wall/reward/account code. Controlled engine services, GameRules ownership, character money and XP consumer. '
               + ('Original zs_2gate wall and round resources, numeric/metadata regression. ' if suite == '2gate' else 'Round difficulty regression. ')
               + 'No complete Gate, full DLL or game.')))
    ast.parse(Path(__file__).read_text('utf-8-sig'))
    for row in input_rows.values():
        assert identity(row['path']) == row, row['path']
    print(json.dumps(dict(prepared=str(out), sources=len(sources), inputs=len(input_rows),
        independentCopies=len(copies), registryAccessors=len(names), compiled=False, executed=False)))


def run(out):
    plan = read(out / 'plan.json')
    assert plan['maxJobs'] == 1 and not (out / 'execution-started.json').exists()
    assert identity(plan['inputs']['path']) == plan['inputs']
    inputs = read(plan['inputs']['path'])
    expected = {os.path.normcase(row['path']): row for row in inputs}
    def verify():
        for row in inputs:
            assert identity(row['path']) == row, row['path']
    verify()
    save(out / 'execution-started.json', dict(plan=identity(out / 'plan.json'), time=time.time()))
    environment = os.environ.copy()
    environment.update({key: value for key, value in plan['environment'].items() if isinstance(value, str)})
    temp = out / 'temp'; temp.mkdir()
    environment.update(TMP=str(temp), TEMP=str(temp), VSLANG='1033')
    started = time.monotonic()
    result = dict(success=False, commands=[], actualIncludes=[], scope=plan['scope'])
    seen = set()
    try:
        for command in plan['commands']:
            remaining = plan['maxSeconds'] - (time.monotonic() - started)
            assert remaining > 0, 'total time budget exhausted'
            before = time.monotonic()
            log = out / (command['name'] + '.log')
            with log.open('xb') as stream:
                completed = subprocess.run(command['argv'], cwd=out, env=environment, stdout=stream,
                    stderr=subprocess.STDOUT, timeout=min(command['timeout'], remaining))
            row = dict(name=command['name'], exit=completed.returncode,
                       seconds=time.monotonic()-before, log=identity(log))
            result['commands'].append(row)
            text = log.read_text('utf-8-sig', errors='replace')
            if command['kind'] == 'compile':
                for path in re.findall(r'Note: including file:\s*(.+)', text):
                    actual = identity(path.strip()); key = os.path.normcase(actual['path'])
                    assert key in expected and expected[key] == actual, 'unfrozen or changed include: ' + actual['path']
                    if key not in seen:
                        result['actualIncludes'].append(actual); seen.add(key)
            assert completed.returncode == 0, command['name'] + ' failed; no later command ran'
            if command['kind'] == 'test':
                match = re.search(r'checks=(\d+) failures=(\d+)', text)
                assert match and int(match[2]) == 0
                row.update(checks=int(match[1]), failures=int(match[2]))
        verify(); result['inputsUnchanged'] = True; result['success'] = True
    except BaseException as error:
        result['error'] = str(error)
        raise
    finally:
        result['seconds'] = time.monotonic() - started
        save(out / 'result.json', result)
        print(json.dumps({k: v for k, v in result.items() if k not in ('actualIncludes', 'commands')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--prepare', type=Path)
    action.add_argument('--run', type=Path)
    parser.add_argument('--suite', choices=('difficulty', '2gate'), default='difficulty')
    parser.add_argument('--common', type=Path)
    args = parser.parse_args()
    if args.prepare:
        prepare(args.prepare.resolve(), args.suite, args.common.resolve() if args.common else None)
    else:
        run(args.run.resolve())
