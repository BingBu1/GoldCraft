#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare the two-unit continuation after a preserved account-header failure."""
from pathlib import Path
import argparse
import hashlib
import json
import os

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve()
    with path.open('rb') as stream:
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha)


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    for ancestor in (path, *path.parents):
        assert not ancestor.is_symlink() and not ancestor.is_junction(), ancestor
    with path.open('xb') as stream:
        stream.write(data)
    assert path.stat().st_nlink == 1


def save(path, value):
    write(path, (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode())


def prepare(previous, out):
    assert previous.is_relative_to(ROOT / 'build') and out.is_relative_to(ROOT / 'build') and not out.exists()
    plan = read(previous / 'plan.json')
    result = read(previous / 'result.json')
    audit = read(previous / 'post-failure-audit.json')
    assert len(plan['commands']) == 20 and len(result['commands']) == 17
    assert all(command['exit'] == 0 for command in result['commands'][:16])
    assert result['commands'][16]['exit'] == 1 and not result['success']
    assert audit['inputsChecked'] == 1062 and not audit['changedInputs'] and audit['compiledSuccessfully'] == 16
    assert audit['result'] == identity(previous / 'result.json')
    assert identity(previous / 'inputs.json') == plan['inputs']
    out.mkdir(parents=True)
    rows = read(previous / 'inputs.json')
    runner = ROOT / 'tests/csol-scenario-walls/prepare-difficulty.py'
    original_runner = next(row for row in rows if Path(row['path']) == runner)
    saved_runner = identity(previous / 'runner-at-failure.py')
    assert (saved_runner['bytes'], saved_runner['sha256']) == (original_runner['bytes'], original_runner['sha256'])
    # The preparer now includes the complete access header; its run() logic
    # and all frozen production/test inputs are unchanged.
    before = Path(saved_runner['path']).read_bytes()
    after = runner.read_bytes()
    old_line = b"        guard + gate + '#include \"../../../../native/csol/server/scenario_money.h\"\\n'))"
    new_line = b"        guard + gate + '#include \"../../../../native/csol/server/scenario_money.h\"\\n'\n        '#include \"../../../../native/csol/server/scenario_entity_access.h\"\\n'))"
    assert before.count(old_line) == 1 and after == before.replace(old_line, new_line)
    inputs = {}
    for row in rows:
        actual = identity(row['path'])
        assert actual == row or Path(row['path']) == runner, row['path']
        inputs[os.path.normcase(actual['path'])] = actual
    def add(path):
        row = identity(path); inputs[os.path.normcase(row['path'])] = row; return row
    for path in (Path(__file__), previous / 'plan.json', previous / 'result.json',
                 previous / 'post-failure-audit.json', previous / 'inputs.json', previous / 'runner-at-failure.py'):
        add(path)
    reuse = []
    for command, unit in zip(plan['commands'][:16], audit['units'][:16]):
        assert command['name'] == unit['name'] and unit['exit'] == 0
        assert identity(command['argv'][-1]) == unit['source']
        for row in unit['includes']:
            assert identity(row['path']) == row
        assert identity(unit['object']['path']) == unit['object']
        reuse.append(add(unit['object']['path']))
    source = Path(plan['commands'][16]['argv'][-1])
    fixed = out / 'source' / source.relative_to(previous / 'source')
    old_include = b'#include "../../../../native/csol/server/scenario_money.h"\n'
    new_include = old_include + b'#include "../../../../native/csol/server/scenario_entity_access.h"\n'
    original = source.read_bytes()
    assert original.count(old_include) == 1
    write(fixed, original.replace(old_include, new_include)); add(fixed)
    headers = []
    for name in ('scenario_breakable_guard.h', 'game_rules_gate.h', 'scenario_money.h', 'scenario_entity_access.h'):
        relative = Path('native/csol/server') / name
        origin = previous / 'source' / relative
        destination = out / 'source' / relative
        write(destination, origin.read_bytes())
        assert identity(destination)['sha256'] == identity(origin)['sha256']
        headers.append(dict(origin=add(origin), independentCopy=add(destination)))
    commands = []
    objects = {}
    for index in (16, 17):
        command = dict(plan['commands'][index])
        argv = list(command['argv'])
        for at, arg in enumerate(argv):
            if arg.startswith(('/Fo', '/Fd')):
                old = Path(arg[3:]); new = out / 'objects' / old.name
                argv[at] = arg[:3] + str(new)
                if arg.startswith('/Fo'):
                    objects[str(old)] = str(new)
        if index == 16:
            argv[-1] = str(fixed)
        command['argv'] = argv
        commands.append(command)
    (out / 'objects').mkdir()
    link = dict(plan['commands'][18]); argv = []
    for arg in link['argv']:
        if arg.startswith(('/out:', '/pdb:')):
            arg = arg[:5] + str(out / Path(arg[5:]).name)
        argv.append(objects.get(arg, arg))
    link['argv'] = argv; commands.append(link)
    test = dict(plan['commands'][19]); test['argv'] = [str(out / 'wall-difficulty.exe')]; commands.append(test)
    proposal = dict(inputs=identity(out / 'inputs.json') if (out / 'inputs.json').exists() else None,
        environment=plan['environment'], commands=commands, maxJobs=1, maxSeconds=360,
        sourceCount=2, reusedObjects=16, reuse=reuse, copiedHeaders=headers,
        sourceFix=dict(original=add(source), fixed=identity(fixed), onlyChange='Include the actual scenario_entity_access.h definition; all extracted method bytes unchanged'),
        priorAudit=identity(previous / 'post-failure-audit.json'), originalPlan=identity(previous / 'plan.json'),
        compiled=False, executed=False, scope=plan['scope'])
    # The real player.cpp already sees this definition through characters.h
    # (line 42 -> characters.h line 3). The extracted account wrapper needs
    # that same complete type without dragging unrelated character code in.
    proposal['productionIncludePath'] = [add(ROOT / 'external/ReGameDLL_CS/regamedll/dlls/player.cpp'),
        add(ROOT / 'native/csol/server/characters.h'), add(ROOT / 'native/csol/server/scenario_entity_access.h')]
    save(out / 'inputs.json', list(inputs.values()))
    proposal['inputs'] = identity(out / 'inputs.json')
    save(out / 'plan.json', proposal)
    for row in inputs.values():
        assert identity(row['path']) == row, row['path']
    print(json.dumps(dict(prepared=str(out), commands=len(commands), sources=2, reusedObjects=16,
        inputs=len(inputs), plan=identity(out / 'plan.json'), compiled=False, executed=False)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--previous', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.previous.resolve(), args.output.resolve())
