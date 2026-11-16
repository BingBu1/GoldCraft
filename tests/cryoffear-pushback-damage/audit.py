#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit consumed SDK inputs; preserve a broad inventory failure separately."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
GROUPS = {'cof-native', 'cof-damage', 'cof-death', 'cof-reentry', 'cof-cancel', 'cof-owner',
          'kick-damage', 'kick-stagger', 'kick-death', 'kick-reentry', 'kick-cancel', 'kick-owner'}
spec = importlib.util.spec_from_file_location('sdk_inputs', ROOT / 'tests/csol-rebuy/run_sdk.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)

def key(path):
    return str(sdk.checked(path)).casefold()

def assess(receipt, driver, declared_unused):
    if receipt.get('sourceCopies') != 0 or receipt.get('deployed') or receipt.get('liveGameplay'):
        raise ValueError('Expected undeployed in-place SDK evidence')
    steps = receipt['steps']
    if [step['name'] for step in steps[:2]] != ['compile', 'link'] or any(step['exit'] for step in steps):
        raise ValueError('Compile/link/behavior did not all execute successfully')
    if {step['name'] for step in steps[2:]} != GROUPS or len(steps) != len(GROUPS) + 2:
        raise ValueError('Exact complete action-damage groups required')
    counts = {}
    for step in steps[2:]:
        text = sdk.checked(step['log']).read_text('utf-8-sig')
        matches = re.findall(r'^behaviorChecks=(\d+) checks=(\d+) failures=0\s*$', text, re.M)
        if len(matches) != 1 or min(map(int, matches[0])) <= 0:
            raise ValueError('Missing successful nonempty behavior result')
        counts[step['name']] = int(matches[0][1])
    seal = sdk.read(receipt['productionSeal'])
    source_rows = sdk.rows(sdk.read(seal['sourceInventory']['path']))
    consumed = {key(receipt['fixture'])}
    consumed.update(key(path) for step in steps[:2] for path in step['dependencies'])
    if not steps[0]['dependencies'] or not steps[1]['dependencies']:
        raise ValueError('Actual compiler and linker dependency records required')
    inputs = {key(row['path']): row for row in receipt['inputsBefore']}
    for path in consumed:
        if path not in inputs:
            raise ValueError('Consumed dependency was not inventoried')
    # Verify every production source, object, library and control, regardless
    # of whether the fixture itself directly includes the source file.
    required = [*source_rows, *seal['linkInputs'], receipt['executable'], *driver['inputs']]
    required += [seal[name] for name in ('receipt', 'sourceInventory', 'log', 'environment', 'artifact', 'compiler', 'linker')]
    for row in required:
        sdk.verify(row)
    unused = {key(path) for path in declared_unused}
    for path in unused:
        actual = Path(path)
        if not actual.is_relative_to(ROOT / 'tests') or actual.suffix.lower() != '.cpp' or path in consumed:
            raise ValueError('Declared unused path is not an unconsumed test translation unit')
    changed = []
    for path, row in inputs.items():
        if sdk.digest(row['path']) == row['sha256']:
            continue
        if path not in unused or path in consumed:
            raise ValueError('Actual or undeclared input changed: ' + row['path'])
        changed.append(row['path'])
    if {key(path) for path in changed} != unused or {key(path) for path in receipt['changedInputs']} != unused:
        raise ValueError('Declared unused drift does not exactly explain the broad inventory failure')
    if receipt.get('success') or receipt.get('inputsUnchanged'):
        raise ValueError('Keep the original failed broad-inventory result intact')
    return dict(scope='Actual compiler/linker plus all production/original/provider inputs',
        actualInputsUnchanged=True, behaviorGroups=len(counts), sdkChecks=sum(counts.values()),
        counts=counts, observedCompilerDependencies=len(steps[0]['dependencies']),
        observedLinkerDependencies=len(steps[1]['dependencies']), productionSources=len(source_rows),
        productionLinkInputs=len(seal['linkInputs']), unusedTestTranslationUnitDrift=changed,
        broadInventoryResult=False, originalReceiptModified=False, deployed=False, liveGameplay=False)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('receipt', 'driver', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--unused-fixture', type=Path, action='append', required=True)
    args = parser.parse_args()
    out = sdk.checked(args.output, exists=False)
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Fresh audit receipt under workspace build required')
    record = assess(sdk.read(args.receipt), sdk.read(args.driver), args.unused_fixture)
    record['inputs'] = [sdk.identity(path) for path in (Path(__file__), args.receipt, args.driver)]
    out.write_text(json.dumps(record, indent=2) + '\n', 'utf8')
    print(json.dumps(record))

if __name__ == '__main__':
    main()
