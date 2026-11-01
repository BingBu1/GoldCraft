"""Extend a scenario resource plan with boss and ordinary monster skill assets.

The compiled catalog reader decodes the actual map table; the normal production
collector resolves model dependencies. No game deployment or asset copy occurs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
from csol_material_stage import resource_name


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def boss_contract(header):
    text = header.read_text(encoding='utf-8-sig')
    keys = ('CSOLOberonMonsterTypeId', 'CSOLOberonBlackHoleModel', 'CSOLOberonBlackHoleDependencies')
    if not any(key in text for key in keys):
        return None  # Stable ordinary-monster builds do not contain this boss.
    type_match = re.search(r'CSOLOberonMonsterTypeId\s*=\s*(\d+)', text)
    model_match = re.search(r'CSOLOberonBlackHoleModel\[\]\s*=\s*"([^"]+)"', text)
    dependencies_match = re.search(r'CSOLOberonBlackHoleDependencies\s*\{(.*?)\}', text, re.S)
    if not all((type_match, model_match, dependencies_match)):
        raise ValueError('Incomplete boss resource contract')
    type_id, model, body = int(type_match[1]), model_match[1], dependencies_match[1]
    dependencies = re.findall(r'"([^"]+)"', body)
    return type_id, model, {model, *dependencies}


def select_boss_models(catalog, type_id, model):
    return [model] if any(row['id'] == type_id for row in catalog['effectiveTypes']) else []


def skill_contract(source):
    """Read the exact skill precache lists; reject unresolved C++ expressions."""
    headers = [('native/csol/server/scenario_monster_skills.h', ''),
               ('native/csol/scenario_monster_effects.hpp', 'csol::monster_effects::')]
    constants = {}
    for name, prefix in headers:
        text = (source / name).read_text(encoding='utf-8-sig')
        for key, value in re.findall(r'inline constexpr const char\*\s+(\w+)\s*=\s*"([^"]+)"', text):
            constants[prefix + key] = value
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    resources = set()
    for variable, prefix in [('asset', ''), ('sound', 'sound/')]:
        lists = re.findall(r'for\s*\(const auto\* ' + variable + r'\s*:\s*\{([^}]+)\}\)\s*\{', session)
        lists = [body for body in lists if
                 ('CSOLMonsterTrapModel' if variable == 'asset' else 'zombi/Heavy_newTrap.wav') in body]
        if len(lists) != 1:
            raise ValueError(f'Expected one monster skill {variable} precache list')
        for token in lists[0].split(','):
            token = token.strip()
            literal = re.fullmatch(r'"([^"]+)"', token)
            value = literal[1] if literal else constants.get(token)
            if value is None:
                raise ValueError(f'Unresolved monster skill asset: {token}')
            resources.add(resource_name(prefix + value))
    return resources


def smoke_event_contract(source):
    """Bind the plugin-owned event to the same source tree as the DLL."""
    header = source / 'native/csol/scenario_smoke.hpp'
    text = header.read_text(encoding='utf-8-sig')
    if 'EventPath' not in text:
        return None  # Earlier builds share the stock smoke event.
    matches = re.findall(r'inline constexpr char EventPath\[\]\s*=\s*"([^"]+)"', text)
    if len(matches) != 1:
        raise ValueError('Incomplete scenario smoke event contract')
    name = resource_name(matches[0])
    if not name.startswith('events/') or not name.endswith('.sc') or name.lower() == 'events/createsmoke.sc':
        raise ValueError('Scenario smoke requires its own event script')
    script = source / 'native/csol' / name
    for part in [script, *script.parents]:
        if part.is_symlink() or part.is_junction():
            raise ValueError(f'Linked event source path: {part}')
    script.resolve(strict=True).relative_to(source)
    if not script.is_file() or script.stat().st_size == 0:
        raise ValueError('Scenario smoke event script is missing or empty')
    return name, script


def command(program, *args):
    result = subprocess.run([str(program.resolve()), *map(str, args)], check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return json.loads(result.stdout)


def dialogue_contract(source, common, reader, wave):
    """Use production decoding/dispatch, never a second Python table parser."""
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    if 'CollectDialogueResources' not in session:
        return set(), None  # Older frozen DLLs do not consume this HUD.
    if reader is None or wave is None:
        raise ValueError('This DLL needs --dialogue-reader and --wave-resource for its HUD download closure')
    name = resource_name(wave)
    if not name.startswith('maps/') or not name.endswith('.cso'):
        raise ValueError('Expected the map catalog wave resource, relative to common/maps')
    header = source / 'native/csol/scenario_round_hud.hpp'
    text = header.read_text(encoding='utf-8-sig')
    match = re.findall(r'char MessagePath\[\]\s*=\s*"([^"]+)"', text)
    if len(match) != 1:
        raise ValueError('Incomplete scenario dialogue table contract')
    messages = resource_name(match[0])
    payload = bytearray()
    inputs = []
    for relative in (name, messages):
        path = common / relative
        for part in (path, *path.parents):
            if part.is_symlink() or part.is_junction():
                raise ValueError(f'Linked dialogue source path: {part}')
        path.resolve(strict=True).relative_to(common)
        data = path.read_bytes()
        if not data or len(data) > 8 * 1024 * 1024:
            raise ValueError(f'Invalid dialogue input size: {relative}')
        payload.extend(struct.pack('<I', len(data)))
        payload.extend(data)
        inputs.append(dict(path=relative, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    result = subprocess.run([str(reader.resolve(strict=True))], input=payload, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    paths = {resource_name(line) for line in result.stdout.decode('utf-8').splitlines()}
    required = {resource_name(value) for value in re.findall(r'"((?:resource|maps)/[^"\n]+)"', text)}
    if not required.issubset(paths) or messages not in paths:
        raise ValueError('Dialogue reader does not contain the selected DLL HUD resource contract')
    return paths, dict(wave=name, inputs=inputs, resources=sorted(paths),
        readerSha256=digest(reader), headerSha256=digest(header))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('common', type=Path)
    parser.add_argument('catalog_reader', type=Path)
    parser.add_argument('collector', type=Path)
    parser.add_argument('previous_plan', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-root', type=Path,
                        help='Use the exact frozen production source for resource contracts')
    parser.add_argument('--dialogue-reader', type=Path,
                        help='Matching csol_scenario_hud_resources executable')
    parser.add_argument('--wave-resource',
                        help='Exact encrypted wave filename from this map catalog, relative to common')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    common = args.common.resolve(strict=True)
    source_root = args.source_root.resolve(strict=True) if args.source_root else root
    header = source_root / 'native/csol/server/scenario_monster_resources.h'
    boss = boss_contract(header)
    smoke_event = smoke_event_contract(source_root)
    dialogue, dialogue_info = dialogue_contract(source_root, common, args.dialogue_reader, args.wave_resource)
    catalog = command(args.catalog_reader, '--catalog', common)
    boss_models = select_boss_models(catalog, boss[0], boss[1]) if boss else []
    models = boss_models.copy()
    entries = command(args.collector, common, '--relocatable-json', *models) if models else []
    actual = {row['path'] for row in entries}
    expected = boss[2] if boss else set()
    if models and actual != expected:
        raise ValueError(f'Boss precheck closure drift: new={actual-expected}, stale={expected-actual}')
    skills = skill_contract(source_root)
    skill_models = sorted(name for name in skills if name.endswith('.mdl'))
    skill_entries = command(args.collector, common, '--relocatable-json', *skill_models) if skill_models else []
    if not set(skill_models).issubset({row['path'] for row in skill_entries}):
        raise ValueError('Collector omitted a monster skill model')
    entries += skill_entries + [{'path': name} for name in sorted(skills) if not name.endswith('.mdl')]
    entries += [{'path': name} for name in sorted(dialogue)]
    models += skill_models
    previous = json.loads(args.previous_plan.read_text(encoding='utf-8-sig'))
    if previous.get('blocked'):
        raise ValueError('Previous resource plan remains blocked')
    rows = {}
    for row in previous['files']:
        name = resource_name(row['destination'])
        path = Path(row['source']).resolve(strict=True)
        if path.stat().st_size != row['bytes'] or digest(path) != row['sha256']:
            raise ValueError(f'Previous source changed: {name}')
        if name.lower() in rows:
            raise ValueError(f'Duplicate destination: {name}')
        rows[name.lower()] = row.copy()
    added = []
    for entry in entries:
        name = resource_name(entry['path'])
        if 'content' in entry:
            raise ValueError(f'Unexpected generated boss asset: {name}')
        source = common / entry.get('source', name)
        source.resolve(strict=True).relative_to(common)
        for part in [source, *source.parents]:
            if part.is_symlink() or part.is_junction():
                raise ValueError(f'Linked source path: {part}')
        row = dict(source=str(source), destination=name, bytes=source.stat().st_size,
                   sha256=digest(source), reuse='verified-common')
        old = rows.get(name.lower())
        if old and (old['sha256'] != row['sha256'] or old['bytes'] != row['bytes']):
            raise ValueError(f'Conflicting previous boss resource: {name}')
        if not old:
            rows[name.lower()] = row
            added.append(name)
    if smoke_event:
        name, script = smoke_event
        row = dict(source=str(script), destination=name, bytes=script.stat().st_size,
                   sha256=digest(script), reuse='verified-project-source')
        old = rows.get(name.lower())
        if old and (old['sha256'] != row['sha256'] or old['bytes'] != row['bytes']):
            raise ValueError(f'Conflicting previous scenario event: {name}')
        if not old:
            rows[name.lower()] = row
            added.append(name)
    result = dict(previous, files=sorted(rows.values(), key=lambda row: row['destination'].lower()),
                  models=sorted(set(previous.get('models', [])) | set(models)),
                  fileCount=len(rows),
                  monsterSkillClosure=dict(roots=sorted(skills), resources=sorted({r['path'] for r in skill_entries} | skills)))
    if boss:
        result['bossClosure'] = dict(typeId=boss[0], roots=boss_models, resources=sorted(actual))
    if smoke_event:
        result['smokeEvent'] = dict(path=smoke_event[0],
            headerSha256=digest(source_root / 'native/csol/scenario_smoke.hpp'),
            scriptSha256=digest(smoke_event[1]))
    if dialogue_info:
        result['dialogueClosure'] = dialogue_info
    args.output.mkdir(parents=True, exist_ok=False)
    def save(name, data):
        (args.output / name).write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    save('resource-plan.json', result)
    closure = command(args.catalog_reader, '--plan', args.output / 'resource-plan.json')
    save('runtime-closure.json', closure)
    save('manifest.json', dict(models=result['models'], effectiveTypes=catalog['effectiveTypes'],
        files=result['files'], added=added, headerSha256=digest(header), bossContractAvailable=boss is not None,
        skillContractSources=[dict(path=name, sha256=digest(source_root / name)) for name in
            ('native/csol/server/scenario_monster_skills.h', 'native/csol/scenario_monster_effects.hpp',
             'native/csol/server/scenario_session.cpp')],
        previousPlanSha256=digest(args.previous_plan), collectorSha256=digest(args.collector),
        catalogReaderSha256=digest(args.catalog_reader), physicalCopies=0, deployment=False))
    print(json.dumps(dict(files=len(rows), added=added, models=len(result['models']),
                         runtimeDependencies=closure['uniqueDependencyFiles']), ensure_ascii=False))


if __name__ == '__main__':
    main()
