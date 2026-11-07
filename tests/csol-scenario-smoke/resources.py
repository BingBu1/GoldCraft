#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual resource-plan writer; native decoder edges are recorded/controlled.

No compiler, game, deployment, network, hard links, or copied game assets.
Outputs must be a new workspace build directory. Existing 93-file evidence is
read-only; new source fixtures contain only tiny independent text files.
"""
from pathlib import Path
import argparse
import contextlib
import datetime
import hashlib
import importlib.util
import io
import json
import sys
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.dont_write_bytecode = True
EVENT = 'events/csol_scenario_smoke.sc'
HEADER = 'native/csol/scenario/core/scenario_smoke.hpp'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(path):
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest(path))


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding='utf-8')


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--common', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build'):
        parser.error('Output must be a new directory inside workspace build')
    for part in (out, *out.parents):
        if part.is_symlink() or part.is_junction():
            parser.error('Linked output directory refused')
    out.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('production_scenario_resources', ROOT / 'tools/csol_scenario_resources.py')
    production = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(production)
    baseline = ROOT / 'build/csol-miniboss-stall-20261030'
    old_source = baseline / 'candidate-08/source'
    old_evidence = baseline / 'resource-plan-validation-02/run1'
    old_plan = old_evidence / 'resource-plan.json'
    old_manifest = load(old_evidence / 'manifest.json')
    old_catalog = dict(effectiveTypes=old_manifest['effectiveTypes'])
    old_collector_path = baseline / 'resource-plan-validation-01/skills.stdout.json'
    old_collector = load(old_collector_path)
    old_closure = load(old_evidence / 'runtime-closure.json')
    production_inputs = [ROOT / 'tools/csol_scenario_resources.py', ROOT / 'tools/csol_material_stage.py',
                         ROOT / HEADER, ROOT / 'native/csol/events/csol_scenario_smoke.sc',
                         Path(__file__).resolve(), old_plan, old_collector_path,
                         old_evidence / 'manifest.json', old_evidence / 'runtime-closure.json',
                         baseline / 'resource-plan-validation-02/handoff.json']
    production_inputs += [old_source / name for name in (HEADER,
        'native/csol/server/scenario/monsters/scenario_monster_resources.h', 'native/csol/server/scenario/monsters/scenario_monster_skills.h',
        'native/csol/scenario/monsters/scenario_monster_effects.hpp', 'native/csol/server/scenario/core/scenario_session.cpp')]
    before = [identity(path) for path in production_inputs]
    assertions = []
    external_calls = []
    checks_file = out / 'assertions.json'

    def check(name, passed):
        assertions.append(dict(name=name, passed=bool(passed)))
        checks_file.write_text(json.dumps(assertions, indent=2), encoding='utf-8')
        if not passed:
            raise AssertionError(name)

    reader = out / 'reader.boundary'; collector = out / 'collector.boundary'
    write(reader, 'Controlled catalog boundary; not an executable.\n')
    write(collector, 'Controlled collector boundary; not an executable.\n')

    def invoke(label, source, common, previous, entries=None, catalog=None, closure=None):
        destination = out / label
        def command(program, *parameters):
            external_calls.append(dict(case=label, program=str(program), arguments=list(map(str, parameters))))
            if parameters[0] == '--catalog':
                return catalog if catalog is not None else {'effectiveTypes': []}
            if parameters[0] == '--plan':
                return closure if closure is not None else {'uniqueDependencyFiles': 1}
            check(label + ': collector called only for expected model roots',
                  parameters[1] == '--relocatable-json')
            return entries if entries is not None else [{'path': 'models/zombitrap.mdl'}]
        argv = ['csol_scenario_resources.py', str(common), str(reader), str(collector),
                str(previous), str(destination), '--source-root', str(source)]
        output = io.StringIO()
        try:
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(production, 'command', command), contextlib.redirect_stdout(output):
                production.main()
        finally:
            write(out / (label + '.stdout.log'), output.getvalue())
        return load(destination / 'resource-plan.json'), load(destination / 'manifest.json')

    old = load(old_plan)
    check('old baseline remains the expected 93-file plan', old['fileCount'] == len(old['files']) == 93)
    check('actual frozen smoke header has no custom event contract', production.smoke_event_contract(old_source) is None)
    actual_old, _ = invoke('old93', old_source, args.common.resolve(), old_plan,
                         entries=old_collector, catalog=old_catalog, closure=old_closure)
    check('actual old frozen source retains all 93 original file records', actual_old['files'] == old['files'] and actual_old['fileCount'] == 93)
    check('old frozen source does not append project event', not any(r['destination'] == EVENT for r in actual_old['files']))
    check('old plan bytes unchanged by current production writer', digest(out / 'old93/resource-plan.json') == digest(old_plan))

    common = out / 'common'; source = out / 'source'
    write(common / 'models/zombitrap.mdl', 'model-decoding is a controlled boundary\n')
    write(common / 'sound/zombi/Heavy_newTrap.wav', 'sound-fixture\n')
    write(common / EVENT, '// common is an intentional decoy; never use it for project event\n')
    write(source / 'native/csol/server/scenario/monsters/scenario_monster_resources.h', '// no boss contract in this fixture\n')
    write(source / 'native/csol/server/scenario/monsters/scenario_monster_skills.h', 'inline constexpr const char* CSOLMonsterTrapModel = "models/zombitrap.mdl";\n')
    write(source / 'native/csol/scenario/monsters/scenario_monster_effects.hpp', '// no extra effect constants\n')
    write(source / 'native/csol/server/scenario/core/scenario_session.cpp',
          'for (const auto* asset : {CSOLMonsterTrapModel}) { }\nfor (const auto* sound : {"zombi/Heavy_newTrap.wav"}) { }\n')
    write(source / HEADER, (ROOT / HEADER).read_text(encoding='utf-8-sig'))
    script = source / 'native/csol' / EVENT
    script_text = '// Fixture source root event, deliberately distinct from workspace and common.\n'
    write(script, script_text)
    empty = out / 'empty-plan.json'; save(empty, dict(files=[], models=[], blocked=[], fileCount=0))
    contract = production.smoke_event_contract(source)
    check('new event contract reads explicitly selected source root', contract == (EVENT, script))
    new, manifest = invoke('new', source, common, empty)
    event_row = next(row for row in new['files'] if row['destination'] == EVENT)
    check('custom event row binds the requested source root not common/workspace', Path(event_row['source']) == script and event_row['sha256'] == digest(script))
    check('new script bytes differ from both decoys', digest(script) != digest(common / EVENT) and digest(script) != digest(ROOT / 'native/csol/events/csol_scenario_smoke.sc'))
    check('event path is added exactly once with project reuse provenance', manifest['added'].count(EVENT) == 1 and event_row['reuse'] == 'verified-project-source')
    check('event audit binds exact selected header and script SHA', new['smokeEvent'] == dict(path=EVENT, headerSha256=digest(source / HEADER), scriptSha256=digest(script)))
    repeat, repeat_manifest = invoke('repeat', source, common, out / 'new/resource-plan.json')
    check('repeating same plan has no added resources', repeat_manifest['added'] == [])
    check('repeating same plan preserves all rows and total', repeat['files'] == new['files'] and repeat['fileCount'] == new['fileCount'])
    check('repeating same plan is byte-stable', digest(out / 'repeat/resource-plan.json') == digest(out / 'new/resource-plan.json'))

    def rejection(label, callback, required=''):
        try:
            callback()
        except (ValueError, OSError) as error:
            check(label + ': explicit rejection', not required or required in str(error))
            save(out / (label + '.error.json'), dict(error=type(error).__name__, message=str(error)))
        else:
            check(label + ': explicit rejection', False)

    header_text = (source / HEADER).read_text(encoding='utf-8')
    for index, invalid in enumerate(('../outside.sc', 'events/../outside.sc', '/events/a.sc',
            'events\\a.sc', 'events/a.sc:payload', 'models/a.sc', 'events/a.sc.exe',
            'events/createsmoke.sc', 'events/CreateSmoke.sc', 'events/' + 'a' * 60 + '.sc')):
        write(source / HEADER, header_text.replace(EVENT, invalid))
        rejection(f'invalid-path-{index:02}', lambda: production.smoke_event_contract(source))
    write(source / HEADER, 'inline constexpr auto EventPath = SomeOtherValue;\n')
    rejection('unresolved-contract', lambda: production.smoke_event_contract(source), 'contract')
    write(source / HEADER, header_text + '\ninline constexpr char EventPath[] = "events/duplicate.sc";\n')
    rejection('duplicate-contract', lambda: production.smoke_event_contract(source), 'contract')
    write(source / HEADER, header_text.replace(EVENT, 'events/missing.sc'))
    rejection('missing-script', lambda: invoke('missing-output', source, common, empty))
    check('missing script rejected before output creation', not (out / 'missing-output').exists())
    write(source / HEADER, header_text); write(script, '')
    rejection('empty-script', lambda: production.smoke_event_contract(source), 'empty')
    write(script, script_text + '// same source changed after prior manifest\n')
    rejection('script-sha-drift', lambda: invoke('drift-output', source, common, out / 'new/resource-plan.json'), 'Previous source changed')
    check('drift rejected before output creation', not (out / 'drift-output').exists())
    write(script, script_text)
    foreign = out / 'different-source.sc'; write(foreign, '// conflicting previously registered bytes\n')
    conflicting = dict(new)
    conflicting['files'] = [dict(row, source=str(foreign), bytes=foreign.stat().st_size, sha256=digest(foreign))
                            if row['destination'] == EVENT else dict(row) for row in new['files']]
    conflict_plan = out / 'conflict-plan.json'; save(conflict_plan, conflicting)
    rejection('event-destination-conflict', lambda: invoke('conflict-output', source, common, conflict_plan), 'Conflicting previous scenario event')
    check('conflict rejected before output creation', not (out / 'conflict-output').exists())
    check('all fixture and source files independent', all(p.stat().st_nlink == 1 for p in source.rglob('*') if p.is_file()))
    after = [identity(path) for path in production_inputs]
    check('production sources and old 116-check evidence unchanged', before == after)
    save(out / 'receipt.json', dict(at=datetime.datetime.now().astimezone().isoformat(), status='PASS',
        checks=len(assertions), failures=sum(not row['passed'] for row in assertions), assertions=assertions,
        inputsBefore=before, inputsAfter=after, inputsUnchanged=before == after,
        controlledNativeCalls=external_calls, compilation=False, games=False, deployment=False, network=False,
        boundary='Actual Python contract and main plan writer. Native catalog/collector are recorded or controlled boundaries, not rerun decoding. Actual old93 files rehashed by production writer; no full SDK or runtime claim.'))
    print(json.dumps(dict(status='PASS', checks=len(assertions), oldFiles=93, newFixtureFiles=new['fileCount'],
                         sourceRoot=str(source), output=str(out)), ensure_ascii=False))


if __name__ == '__main__':
    main()
