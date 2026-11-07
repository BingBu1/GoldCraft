#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare an add-only scenario map plan using production readers; never deploy."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
from csol_material_stage import prepare_materials, resource_name
from ServerPackageMaps import map_resources
import csol_scenario_resources as contracts

ROOT = Path(__file__).resolve().parent.parent


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def plain(path):
    path = Path(path).absolute()
    for p in (path, *path.parents):
        if p.is_symlink() or p.is_junction():
            raise ValueError('linked path: ' + str(p))
    return path.resolve(strict=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('common', 'server', 'reader', 'collector', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--map', default='zs_lastclue')
    args = parser.parse_args()
    common, server = plain(args.common), plain(args.server)
    out = args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('fresh workspace build output required')
    for p in (out, *out.parents):
        if p.is_symlink() or p.is_junction(): raise ValueError('linked output')
    if not re.fullmatch('[a-z0-9_]{1,63}', args.map): raise ValueError('invalid map')
    out.mkdir(parents=True)
    def save(name, data):
        (out / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), 'utf-8')
    inputs = {}
    def read(path):
        path = plain(path); data = path.read_bytes()
        inputs[str(path)] = identity(path)
        return data
    def command(exe, *values):
        ran = subprocess.run([str(plain(exe)), *map(str, values)], capture_output=True, check=True)
        if ran.stderr: (out / (exe.stem + '-stderr.log')).write_bytes(ran.stderr)
        return json.loads(ran.stdout)
    inventory = command(args.reader, common, args.map)
    save('catalog.json', inventory)
    if not inventory['supported']: raise ValueError('map is not admitted by production gameplay')
    spec = importlib.util.spec_from_file_location('map_music', ROOT / 'tools/Prepare-CSOLScenarioMusic.py')
    music = importlib.util.module_from_spec(spec); spec.loader.exec_module(music)
    bsp = common / 'maps' / (args.map + '.bsp')
    entities = music.entities(read(bsp))
    base, bsp_info = map_resources(bsp, common, common, plain)
    roots = set(inventory['models'])
    required = set(inventory['resources']) | set(inventory['bgm'])
    required.update(p.relative_to(common).as_posix() for p in base.values())
    required.update(p for p in inventory['tableReads'] if (common / p).is_file())
    required.update('maps/' + args.map + suffix for suffix in ('.nav', '_detail.txt'))
    # These optional original table companions are opened by the host map/skills loaders.
    for prefix in ('zbs_sp_', 'zbs_map_trg_', 'ZBS_Difficulty_'):
        path = 'maps/' + prefix + args.map + '.cso'
        if (common / path).is_file(): required.add(path)
    for suffix in ('.txt', '.bmp'):
        path = 'overviews/' + args.map + suffix
        if (common / path).is_file(): required.add(path)
    for e in entities:
        model = e.get('model', '')
        if model.startswith(('models/', 'sprites/')):
            roots.add(model)
        if e.get('classname') == 'ambient_generic':
            sound = e.get('message', '')
            if sound and not sound.startswith('!'): required.add('sound/' + sound)
        if e.get('classname') == 'trigger_bgm':
            raise ValueError('dynamic BSP BGM requires production music path reader')
    sky = entities[0].get('skyname', '')
    if sky:
        required.update('gfx/env/' + sky + face + '.tga' for face in ('bk', 'dn', 'ft', 'lf', 'rt', 'up'))
    detail = common / ('maps/' + args.map + '_detail.txt')
    for line in read(detail).decode('latin1').splitlines():
        tokens = line.split('//', 1)[0].split()
        if tokens:
            if len(tokens) != 4: raise ValueError('unexpected detail texture row')
            required.add('gfx/' + tokens[1] + '.tga')
    # Reuse existing validated source-contract collectors, without decrypting tables in Python.
    required.update(contracts.skill_contract(ROOT))
    required.update(contracts.elite_contract(ROOT))
    wall, _ = contracts.wall_upgrade_contract(ROOT); required.update(wall)
    # These shared lists are already in the active server; bind their existing bytes.
    for filename, symbol in [('native/csol/scenario/monsters/frozen_death.hpp', 'IceModel'),
                             ('native/csol/scenario/monsters/frozen_smoke.hpp', 'SmokeSprite')]:
        text = read(ROOT / filename).decode('utf-8-sig')
        values = re.findall(r'char ' + symbol + r'\[\]\s*=\s*"([^"]+)"', text)
        if len(values) != 1: raise ValueError('missing frozen contract')
        required.add(values[0])
    roots.update(n for n in required if n.endswith(('.mdl', '.spr')))
    entries = command(args.collector, common, '--relocatable-json', *sorted(roots))
    save('original-model-closure.json', entries)
    payloads, conversions = prepare_materials(entries, lambda n: read(common / n))
    required.update(payloads)
    for n in inventory['tableReads']:
        if (common / n).exists(): read(common / n)
    rows, blocked, preserved = [], [], []
    for name in sorted(required):
        resource_name(name)
        destination = server / name
        source = common / name
        proposed = payloads.get(name)
        if proposed is None and source.is_file(): proposed = read(source)
        if destination.is_file():
            selected = identity(plain(destination))
            incoming_sha = hashlib.sha256(proposed).hexdigest() if proposed is not None else None
            action = 'reuse' if incoming_sha == selected['sha256'] else 'preserve-existing'
            if action != 'reuse': preserved.append(dict(path=name, incomingSha256=incoming_sha, existing=selected))
            rows.append(dict(destination=name, source=selected['path'], bytes=selected['bytes'], sha256=selected['sha256'], action=action))
        elif proposed is None:
            blocked.append(dict(path=name, reason='absent from common and installed server'))
        else:
            digest = hashlib.sha256(proposed).hexdigest()
            if name in payloads and (not source.is_file() or read(source) != proposed):
                source = out / 'derived' / name; source.parent.mkdir(parents=True, exist_ok=True)
                with source.open('xb') as f: f.write(proposed)
            rows.append(dict(destination=name, source=str(source), bytes=len(proposed), sha256=digest, action='add'))
    for row in rows:
        if identity(row['source']) != dict(path=str(Path(row['source']).resolve()), bytes=row['bytes'], sha256=row['sha256']):
            raise ValueError('selected source changed')
    result = dict(map=args.map, mapId=inventory['id'], files=rows, models=sorted(roots), blocked=blocked,
                  preserved=preserved, conversions=conversions, bsp=bsp_info,
                  entityClasses=dict(Counter(e.get('classname') for e in entities)),
                  reader=identity(args.reader), collector=identity(args.collector), inputs=list(inputs.values()),
                  deployment=False, finalModelValidation=False,
                  scope='Map-specific and ordinary/champion/skill resources against an existing scenario installation; not a standalone server package')
    save('resource-plan.json', result)
    save('add-only-plan.json', [r for r in rows if r['action']=='add'])
    validation = command(args.reader, '--plan', out / 'resource-plan.json')
    save('final-model-validation.json', validation)
    result['finalModelValidation'] = validation
    result['changedInputs'] = [item['path'] for item in inputs.values() if identity(item['path']) != item]
    result['toolSources'] = [identity(Path(__file__)), identity(ROOT / 'tools/csol_material_stage.py'),
                             identity(ROOT / 'tools/csol_scenario_resources.py'), identity(ROOT / 'tools/ServerPackageMaps.py'),
                             identity(ROOT / 'tools/Prepare-CSOLScenarioMusic.py')]
    if result['changedInputs']: raise ValueError('resource inputs changed during planning')
    save('resource-plan.json', result)
    print(json.dumps(dict(files=len(rows), added=sum(r['action']=='add' for r in rows), blocked=blocked,
                          preserved=len(preserved), models=len(roots)), ensure_ascii=False))
    return bool(blocked)


if __name__ == '__main__':
    raise SystemExit(main())
