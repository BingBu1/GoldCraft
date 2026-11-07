#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real wall-resource CLI merge with an actual MDL collector.

Only unrelated monster/HUD contracts and the map-catalog executable are stubbed.
Native inputs come from a verified effective source manifest. The current Python
tools are frozen separately because a DLL manifest can retain an older tool.
No compilation, resource installation, or game process is performed.
"""
from contextlib import ExitStack, redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch
import argparse
import copy
import hashlib
import importlib
import io
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SCHEME = 'resource/csol/wall/v1/WallUpgradeScheme.res'
MODEL = 'models/ze_machine.mdl'
EXPECTED = {
    MODEL: (47280, 'd94cd2829b1a307eff3055ac74c54384c9ada83d816259630389c791d36fadb7'),
    'sprites/e_button01.spr':
        (17214, 'd7e080e926fb6f49506a6e7fefbe483e2d0c18b357ae4550966eff9adc7ad1a7'),
    'resource/Hud/ZBS/zbslevelupwallbg.tga':
        (22892, '22b25d73865f9771b7952be42d148d2b2304d2e9ffc866a1080e8d1d48d72181'),
    SCHEME: (222, '570c4a2ea8238ca635195c8a788afb18050f0bfb4deff516459552d1ffef3bf2'),
}
NATIVE_INPUTS = (
    'native/csol/server/scenario/core/scenario_session.cpp',
    'native/csol/scenario/core/wall_upgrade.hpp',
    'native/csol/server/scenario/monsters/scenario_monster_resources.h',
)
TOOL_INPUTS = ('tools/csol_scenario_resources.py', 'tools/csol_material_stage.py')


def ordinary_path(path):
    path = path.absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError(f'Linked fixture input/output: {part}')
    return path


def identity(path):
    path = ordinary_path(path).resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def freeze(source, target, expected=None):
    before = identity(source)
    if expected is not None:
        assert before == expected, ('Manifest identity changed', before, expected)
    ordinary_path(target).parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as stream:
        stream.write(source.read_bytes())
    frozen = identity(target)
    assert identity(source) == before
    assert target.stat().st_nlink == 1
    assert (before['bytes'], before['sha256']) == (frozen['bytes'], frozen['sha256'])
    return dict(origin=before, frozen=frozen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-manifest', type=Path, required=True)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--collector', type=Path, required=True)
    parser.add_argument('--wall-scheme', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tool-root', type=Path, default=ROOT)
    args = parser.parse_args()
    out = ordinary_path(args.output).resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir(parents=True)
    snapshot = out / 'source'
    common = ordinary_path(args.common).resolve(strict=True)
    prepared = ordinary_path(args.wall_scheme).resolve(strict=True)
    tool_root = ordinary_path(args.tool_root).resolve(strict=True)
    manifest_record = freeze(args.source_manifest, out / 'effective-source-manifest.json')
    manifest = json.loads((out / 'effective-source-manifest.json').read_text(encoding='utf-8-sig'))
    selected = {}
    for row in manifest['files']:
        assert row['relative'] not in selected, ('Duplicate manifest input', row['relative'])
        selected[row['relative']] = row['frozen']
    inputs = []
    for name in NATIVE_INPUTS:
        record = selected[name]
        inputs.append(dict(relative=name, selection='effective-source-manifest',
                           **freeze(Path(record['path']), snapshot / name, record)))
    tool_manifest_comparison = []
    for name in TOOL_INPUTS:
        record = freeze(tool_root / name, snapshot / name)
        inputs.append(dict(relative=name, selection='current-resource-tool', **record))
        old = selected.get(name)
        if old:
            assert identity(Path(old['path'])) == old
        tool_manifest_comparison.append(dict(relative=name, manifest=old, tested=record['origin'],
            sameAsManifest=bool(old and old['sha256'] == record['origin']['sha256'])))
    fixture = freeze(Path(__file__), out / 'fixture' / Path(__file__).name)
    collector_record = freeze(args.collector, out / 'collector' / args.collector.name)
    collector = Path(collector_record['frozen']['path'])
    resources_before = {}
    for name, expected in EXPECTED.items():
        record = identity((prepared if name == SCHEME else common) / name)
        assert (record['bytes'], record['sha256']) == expected, name
        resources_before[name] = record
    common_scheme = common / SCHEME
    common_scheme_before = identity(common_scheme) if common_scheme.exists() else None

    # This data file is a sentinel for the explicit subprocess boundary below;
    # it is never executed and is not a real catalog-reader validation result.
    catalog = out / 'map-catalog-boundary.json'
    save(catalog, dict(testBoundary='unrelated map catalog', executable=False,
                       outputs=['--catalog', '--plan']))
    empty = out / 'empty-plan.json'
    save(empty, dict(blocked=False, files=[], models=[]))
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(snapshot / 'tools'))
    module = importlib.import_module('csol_scenario_resources')
    material = importlib.import_module('csol_material_stage')
    assert Path(module.__file__).resolve() == snapshot / TOOL_INPUTS[0]
    assert Path(material.__file__).resolve() == snapshot / TOOL_INPUTS[1]
    assert module.wall_upgrade_contract(snapshot)[0] == set(EXPECTED)
    real_run = subprocess.run
    boundaries = {
        'boss_contract': None,
        'smoke_event_contract': None,
        'dialogue_contract': (set(), None),
        'respawn_ui_contract': (set(), None),
        'champion_contract': (set(), None),
        'elite_contract': set(),
        'frozen_death_contract': (set(), []),
        'skill_contract': set(),
        'skill_contract_sources': [],
    }
    cases = []

    def run_case(label, previous, scheme=None, error=None):
        case = out / label
        case.mkdir()
        output = case / 'output'
        argv = [module.__file__, str(common), str(catalog), str(collector),
                str(previous), str(output), '--source-root', str(snapshot)]
        if scheme is not None:
            argv += ['--wall-scheme', str(scheme)]
        calls = []
        previous_before = identity(previous)

        def subprocess_boundary(command, *positional, **keywords):
            command = list(map(str, command))
            if command[0] == str(catalog):
                if command[1:] == ['--catalog', str(common)]:
                    data = dict(effectiveTypes=[], testBoundary='unrelated map catalog')
                elif command[1:] == ['--plan', str(output / 'resource-plan.json')]:
                    data = dict(uniqueDependencyFiles=0, testBoundary='unrelated map plan reader')
                else:
                    raise AssertionError(('Unexpected catalog boundary call', command))
                calls.append(dict(command=command, stub=True, result=data))
                return subprocess.CompletedProcess(command, 0, json.dumps(data).encode('utf-8'), b'')
            assert command == [str(collector), str(common), '--relocatable-json', MODEL], command
            done = real_run(command, *positional, **keywords)
            (case / 'collector.stdout.json').write_bytes(done.stdout)
            (case / 'collector.stderr.txt').write_bytes(done.stderr)
            entries = json.loads(done.stdout)
            assert entries == [dict(path=MODEL, source=MODEL)], entries
            calls.append(dict(command=command, stub=False, exitCode=done.returncode,
                              resources=entries))
            return done

        stdout, stderr = io.StringIO(), io.StringIO()
        caught = None
        with ExitStack() as stack:
            mocks = {name: stack.enter_context(patch.object(module, name, return_value=value))
                     for name, value in boundaries.items()}
            stack.enter_context(patch.object(module.subprocess, 'run', side_effect=subprocess_boundary))
            stack.enter_context(patch.object(sys, 'argv', argv))
            stack.enter_context(redirect_stdout(stdout))
            stack.enter_context(redirect_stderr(stderr))
            try:
                module.main()
            except ValueError as failure:
                caught = str(failure)
        (case / 'stdout.txt').write_text(stdout.getvalue(), encoding='utf-8')
        (case / 'stderr.txt').write_text(stderr.getvalue(), encoding='utf-8')
        save(case / 'execution.json', dict(argv=argv, calls=calls, error=caught,
             contractBoundaryCalls={name: mock.call_count for name, mock in mocks.items()}))
        assert caught == error, (label, caught, error)
        assert identity(previous) == previous_before
        assert sum(not call['stub'] for call in calls) == 1
        for name, mock in mocks.items():
            assert mock.call_count == (0 if error and name == 'skill_contract_sources' else 1), name
        if error:
            assert not output.exists(), ('Rejected case wrote a plan', label)
            assert not stdout.getvalue()
        else:
            plan = json.loads((output / 'resource-plan.json').read_text(encoding='utf-8'))
            assert plan['fileCount'] == len(plan['files']) == 4
            assert {row['destination'] for row in plan['files']} == set(EXPECTED)
            assert plan['models'] == [MODEL]
            assert plan['wallUpgradeClosure'] == dict(roots=sorted(EXPECTED), contract=dict(
                path='native/csol/scenario/core/wall_upgrade.hpp', sha256=selected['native/csol/scenario/core/wall_upgrade.hpp']['sha256']))
            assert plan['monsterSkillClosure'] == dict(roots=sorted(EXPECTED), resources=sorted(EXPECTED))
            for row in plan['files']:
                name = row['destination']
                assert (row['bytes'], row['sha256']) == EXPECTED[name]
                assert identity(Path(row['source'])) == resources_before[name]
                assert row['reuse'] == ('verified-wall-scheme-v1' if name == SCHEME else 'verified-common')
            emitted = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
            assert emitted['physicalCopies'] == 0 and emitted['deployment'] is False
            assert emitted['collectorSha256'] == collector_record['frozen']['sha256']
            assert emitted['previousPlanSha256'] == previous_before['sha256']
            assert set(emitted['added']) == (set(EXPECTED) if label == 'explicit-scheme' else set())
            assert json.loads(stdout.getvalue()) == dict(files=4, added=emitted['added'],
                                                         models=1, runtimeDependencies=0)
        cases.append(dict(name=label, success=True, expectedError=error,
                          execution=identity(case / 'execution.json'),
                          output=str(output) if error is None else None))
        return output / 'resource-plan.json'

    first = run_case('explicit-scheme', empty, prepared)
    reused = run_case('previous-plan-reuse', first)
    assert json.loads(first.read_text('utf-8')) == json.loads(reused.read_text('utf-8'))
    run_case('missing-scheme', empty, error=
        'Prepare the wall font scheme with Prepare-CSOLWallScheme.py and pass --wall-scheme')

    # A self-consistent old plan contains different, same-length scheme bytes.
    # It passes old-source validation, then must fail the actual SHA merge gate.
    different_scheme = out / 'conflicting-source' / SCHEME
    different_scheme.parent.mkdir(parents=True)
    data = bytearray(Path(resources_before[SCHEME]['path']).read_bytes())
    data[0] ^= 1
    different_scheme.write_bytes(data)
    different = identity(different_scheme)
    assert different['bytes'] == EXPECTED[SCHEME][0] and different['sha256'] != EXPECTED[SCHEME][1]
    conflicting = json.loads(first.read_text(encoding='utf-8'))
    row = next(row for row in conflicting['files'] if row['destination'] == SCHEME)
    row.update(source=different['path'], bytes=different['bytes'], sha256=different['sha256'])
    conflicting_plan = out / 'conflicting-plan.json'
    save(conflicting_plan, conflicting)
    run_case('conflicting-sha', conflicting_plan, prepared,
             error='Conflicting previous boss resource: ' + SCHEME)

    stale = copy.deepcopy(conflicting)
    next(row for row in stale['files'] if row['destination'] == SCHEME)['sha256'] = EXPECTED[SCHEME][1]
    stale_plan = out / 'stale-sha-plan.json'
    save(stale_plan, stale)
    run_case('stale-previous-sha', stale_plan, prepared, error='Previous source changed: ' + SCHEME)

    for record in [manifest_record, fixture, collector_record, *inputs]:
        for side in ('origin', 'frozen'):
            assert identity(Path(record[side]['path'])) == record[side]
    for record in resources_before.values():
        assert identity(Path(record['path'])) == record
    assert (identity(common_scheme) if common_scheme.exists() else None) == common_scheme_before
    result = dict(success=True, cases=cases, resourceCount=4,
        negativeCases=3, actualCollectorRuns=len(cases), inputs=inputs,
        sourceManifest=manifest_record, toolManifestComparison=tool_manifest_comparison,
        fixture=fixture, collector=collector_record, resources=resources_before,
        boundaries=dict(contractFunctions=list(boundaries),
            subprocess='Only --catalog/--plan use a sentinel; MDL collector executes unchanged.',
            limitation='Map catalog/runtime-closure outputs are stubs, not runtime closure acceptance.'),
        realCode=['main', 'command', 'wall_upgrade_contract', 'wall_scheme_source', 'resource_name',
                  'previous-plan validation', 'resource merge/conflict checks', 'plan/manifest writers'],
        allInputIdentitiesUnchanged=True, commonSchemeUnchanged=True,
        compiled=False, deployed=False, gameplay=False)
    save(out / 'result.json', result)
    print(json.dumps(dict(success=True, resources=4, cliCases=len(cases), rejected=3,
                          actualCollectorRuns=len(cases), output=str(out))))


if __name__ == '__main__':
    main()
