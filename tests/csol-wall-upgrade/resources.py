#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the actual model closure and frozen wall-machine precache contract."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from csol_scenario_resources import wall_upgrade_contract, wall_scheme_source

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--common', type=Path, required=True)
parser.add_argument('--collector', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--wall-scheme', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
assert out.is_relative_to(ROOT / 'build') and not out.exists()
out.mkdir(parents=True)
snapshot = out / 'source'
inputs = []
def identity(path):
    return dict(path=str(path.resolve()), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

for name in ('native/csol/server/scenario_session.cpp', 'native/csol/wall_upgrade.hpp'):
    path = ROOT / name
    original = identity(path)
    target = snapshot / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(path.read_bytes())
    assert identity(path) == original and target.stat().st_nlink == 1
    inputs.append(dict(origin=original, frozen=identity(target)))
roots, contract = wall_upgrade_contract(snapshot)
assert roots == {'models/ze_machine.mdl', 'sprites/e_button01.spr',
                 'resource/Hud/ZBS/zbslevelupwallbg.tga',
                 'resource/csol/wall/v1/WallUpgradeScheme.res'}
collector = args.collector.resolve(strict=True)
before = identity(collector)
done = subprocess.run([str(collector), str(args.common.resolve(strict=True)),
                       '--relocatable-json', 'models/ze_machine.mdl'], check=True, capture_output=True)
(out / 'collector.json').write_bytes(done.stdout)
closure = json.loads(done.stdout)
assert 'models/ze_machine.mdl' in {row['path'] for row in closure}
resources = []
for name in sorted(roots | {row['path'] for row in closure}):
    derived = name == 'resource/csol/wall/v1/WallUpgradeScheme.res'
    path = (wall_scheme_source(args.wall_scheme, {}) if derived
            else (args.common / name).resolve(strict=True))
    assert (derived or path.is_relative_to(args.common.resolve())) and path.stat().st_size > 0
    resources.append(dict(resource=name, **identity(path)))

header = snapshot / 'native/csol/wall_upgrade.hpp'
original = header.read_bytes()
negative = []
for label, old, replacement in (
    ('missing sprite declaration', 'char Sprite[]', 'char MissingSprite[]'),
    ('wrong sprite type', 'sprites/e_button01.spr', 'sprites/e_button01.wav'),
    ('invalid model traversal', 'models/ze_machine.mdl', 'models/../ze_machine.mdl')):
    header.write_text(original.decode('utf-8-sig').replace(old, replacement), encoding='utf-8')
    try:
        wall_upgrade_contract(snapshot)
    except ValueError:
        negative.append(label)
    else:
        raise AssertionError('accepted invalid resource contract: ' + label)
    finally:
        header.write_bytes(original)
assert identity(collector) == before
assert all(identity(Path(row['origin']['path'])) == row['origin'] for row in inputs)
scheme_name = 'resource/csol/wall/v1/WallUpgradeScheme.res'
scheme = wall_scheme_source(args.wall_scheme, {})
assert wall_scheme_source(None, {scheme_name.lower(): {'source': str(scheme)}}) == scheme
try:
    wall_scheme_source(None, {})
except ValueError:
    negative.append('missing derived wall scheme')
else:
    raise AssertionError('accepted missing derived wall scheme')
bad_scheme = out / 'bad-scheme' / scheme_name
bad_scheme.parent.mkdir(parents=True)
bad_scheme.write_bytes(scheme.read_bytes().replace(b'"tall"', b'"fail"'))
assert bad_scheme.read_bytes() != scheme.read_bytes()
try:
    wall_scheme_source(out / 'bad-scheme', {})
except ValueError:
    negative.append('changed derived wall scheme')
else:
    raise AssertionError('accepted changed derived wall scheme')
result = dict(success=True, roots=sorted(roots), contract=contract, resources=resources,
              negativeCases=negative, inputs=inputs, collector=before,
              fullDll=False, deployed=False, gameplay=False)
(out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(dict(resources=len(resources), rejectedContracts=len(negative), success=True)))
