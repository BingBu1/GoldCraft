"""Check the production skill resource plan against real assets and bad contracts."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from csol_scenario_resources import skill_contract, skill_contract_sources

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT)
parser.add_argument('--common', type=Path, required=True)
parser.add_argument('--collector', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source, common = args.source.resolve(strict=True), args.common.resolve(strict=True)
out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
snapshot = out/'source'
inputs = []
for name in skill_contract_sources(source):
    data = (source/name).read_bytes()
    path = snapshot/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
    inputs.append(dict(path=name, sha256=hashlib.sha256(data).hexdigest()))
assets = skill_contract(snapshot)
expected = {'events/createexplo.sc', 'models/grenade.mdl', 'models/w_zombibomb.mdl',
    'sprites/zombiebomb.spr', 'sound/zombi/Zombi_Bomb_Exp.wav',
    'sound/zombi/Zombi_Bomb_Bounce_1.wav', 'sound/zombi/Zombi_Bomb_Bounce_2.wav',
    'sprites/weaponparts_critical.spr', 'sprites/weaponparts_healban.spr',
    'sprites/weaponparts_slow.spr', 'sprites/weaponparts_zbombdown.spr'}
assert expected <= assets, expected-assets
models = sorted(name for name in assets if name.endswith('.mdl'))
proc = subprocess.run([str(args.collector.resolve(strict=True)), str(common),
    '--relocatable-json', *models], check=True, capture_output=True)
(out/'collector.json').write_bytes(proc.stdout)
closure = json.loads(proc.stdout)
assert set(models) <= {row['path'] for row in closure}
resources = assets | {row['path'] for row in closure}
rows = []
for name in sorted(resources):
    path = (common/name).resolve(strict=True); path.relative_to(common)
    assert path.is_file() and path.stat().st_size > 0, name
    rows.append(dict(path=name, bytes=path.stat().st_size,
        sha256=hashlib.sha256(path.read_bytes()).hexdigest()))

negative = []
def rejects(name, file, transform, exception=ValueError):
    path = snapshot/file; original = path.read_bytes()
    path.write_text(transform(original.decode('utf8')), 'utf8')
    try:
        try: skill_contract(snapshot)
        except exception: negative.append(name)
        else: raise AssertionError('accepted malformed contract: '+name)
    finally: path.write_bytes(original)

bomb = 'native/csol/server/scenario_zombie_bomb.h'
parts = 'native/csol/scenario_part_effects.hpp'
rejects('missing model symbol', bomb, lambda s:s.replace('CSOLZombieBombModel =','MissingModel ='))
rejects('missing second bounce', bomb, lambda s:s.replace(', "zombi/Zombi_Bomb_Bounce_2.wav"',''))
rejects('unresolved bounce expression', bomb, lambda s:s.replace('"zombi/Zombi_Bomb_Bounce_1.wav"','OtherSound'))
rejects('invalid event extension', bomb, lambda s:s.replace('events/createexplo.sc','events/createexplo.txt'))
rejects('event traversal', bomb, lambda s:s.replace('events/createexplo.sc','events/../createexplo.sc'))
rejects('unknown part expression', parts, lambda s:s.replace('"sprites/weaponparts_slow.spr"','UnknownSprite'))
rejects('wrong part extension', parts, lambda s:s.replace('sprites/weaponparts_slow.spr','sprites/weaponparts_slow.wav'))
assert skill_contract(snapshot)==assets
for row in inputs:
    assert hashlib.sha256((source/row['path']).read_bytes()).hexdigest()==row['sha256']
result = dict(roots=sorted(assets), resources=rows, negativeCases=negative, inputs=inputs,
    collectorSha256=hashlib.sha256(args.collector.read_bytes()).hexdigest(), failures=0,
    deployed=False, boundary='Read-only production collector and contract validation; no HTTP transfer or gameplay.')
(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf8')
print(json.dumps(dict(roots=len(assets),resources=len(rows),negative=len(negative),failures=0)))
