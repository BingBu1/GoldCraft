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
    expected = {model, *dependencies}
    if 'CSOLOberonKnifeEffectModel' in text:
        knife = re.findall(r'CSOLOberonKnifeEffectModel\[\]\s*=\s*"([^"]+)"', text)
        if len(knife) != 1:
            raise ValueError('Incomplete Oberon knife effect resource contract')
        name = resource_name(knife[0])
        if not name.startswith('models/') or not name.endswith('.mdl'):
            raise ValueError('Oberon knife effect must be a model resource')
        expected.add(name)
    return type_id, model, expected


def boss_attack_contract(source):
    """Read the map-load attack contract from the selected DLL source tree."""
    header = source / 'native/csol/server/scenario_oberon_resources.h'
    if not header.exists():
        return set(), None  # Older frozen builds do not own these attacks.
    text = header.read_text(encoding='utf-8-sig')
    keys = ('CSOLOberonAttackModels', 'CSOLOberonAttackSounds')
    if not any(key in text for key in keys):
        return set(), None
    pairs = re.findall(r'inline constexpr char (\w+)\[\]\s*=\s*"([^"\n]+)";', text)
    constants = dict(pairs)
    if len(constants) != len(pairs):
        raise ValueError('Duplicate Oberon attack resource constant')
    result = set()
    for key, prefix in zip(keys, ('', 'sound/')):
        blocks = re.findall(r'\b' + key + r'\s*\{([^}]+)\}', text, re.S)
        if len(blocks) != 1:
            raise ValueError(f'Missing or ambiguous Oberon attack contract: {key}')
        for token in blocks[0].split(','):
            value = constants.get(token.strip())
            if value is None:
                raise ValueError(f'Unresolved Oberon attack resource: {token}')
            path = resource_name(prefix + value)
            if prefix:
                valid = path.startswith('sound/') and path.endswith('.wav')
            else:
                valid = ((path.startswith('models/') and path.endswith('.mdl')) or
                         (path.startswith('sprites/') and path.endswith('.spr')))
            if not valid or path in result:
                raise ValueError(f'Invalid or duplicate Oberon attack resource: {path}')
            result.add(path)
    return result, dict(path=str(header), sha256=digest(header))


def boss_hint_contract(source):
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    if 'csol::oberon_hint::Sprite' not in session:
        return set(), None  # This frozen source has no hint precheck/consumer.
    header = source / 'native/csol/oberon_hint.hpp'
    values = re.findall(r'char Sprite\[\]\s*=\s*"([^"]+)"', header.read_text(encoding='utf-8-sig'))
    if len(values) != 1:
        raise ValueError('Incomplete Oberon start hint sprite contract')
    name = resource_name(values[0])
    if not name.startswith('sprites/') or not name.endswith('.spr'):
        raise ValueError('Oberon start hint must be a sprite resource')
    return {name}, dict(path='native/csol/oberon_hint.hpp', sha256=digest(header))


def select_boss_models(catalog, type_id, model):
    return [model] if any(row['id'] == type_id for row in catalog['effectiveTypes']) else []


def wall_upgrade_contract(source):
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    if 'csol::wall_upgrade::Model' not in session:
        return set(), None
    name = 'native/csol/wall_upgrade.hpp'
    header = source / name
    text = header.read_text(encoding='utf-8-sig')
    resources = set()
    for symbol, prefix, suffix in [('Model', 'models/', '.mdl'), ('Sprite', 'sprites/', '.spr')]:
        values = re.findall(r'char ' + symbol + r'\[\]\s*=\s*"([^"]+)"', text)
        if len(values) != 1:
            raise ValueError(f'Incomplete wall-upgrade resource contract: {symbol}')
        resource = resource_name(values[0])
        if not resource.startswith(prefix) or not resource.endswith(suffix):
            raise ValueError(f'Invalid wall-upgrade resource: {resource}')
        resources.add(resource)
    # Follow the exact server download consumer. Older frozen source sets
    # have the machine without its level panel, so do not add a new asset to
    # their manifests retroactively.
    for panel in ('resource/Hud/ZBS/zbslevelupwallbg.tga',
                  'resource/csol/wall/v1/WallUpgradeScheme.res'):
        if re.search(r'hudPaths\.emplace\(\s*"' + re.escape(panel) + r'"\s*\)', session):
            resources.add(panel)
    return resources, dict(path=name, sha256=digest(header))


def wall_scheme_source(prepared, previous):
    """Resolve the generated v1 scheme, never an assumed file in common."""
    name = 'resource/csol/wall/v1/WallUpgradeScheme.res'
    if prepared is not None:
        path = Path(prepared).absolute() / name
    elif name.lower() in previous:
        path = Path(previous[name.lower()]['source']).absolute()
    else:
        raise ValueError('Prepare the wall font scheme with Prepare-CSOLWallScheme.py and pass --wall-scheme')
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError(f'Linked wall scheme source: {part}')
    # Exact output of the verified original Default block plus the v1 wrapper.
    # Accepting a different layout requires a new resource revision/contract.
    if path.stat().st_size != 222 or digest(path) != '570c4a2ea8238ca635195c8a788afb18050f0bfb4deff516459552d1ffef3bf2':
        raise ValueError('Wall font scheme v1 identity changed')
    return path.resolve(strict=True)


def frozen_death_contract(source):
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    if 'csol::frozen_death::IceModel' not in session:
        return set(), []  # Older frozen DLLs do not consume these messages.
    resources, inputs = set(), []
    for filename, symbol, prefix, suffix in [
            ('frozen_death.hpp', 'IceModel', 'models/', '.mdl'),
            ('frozen_smoke.hpp', 'SmokeSprite', 'sprites/', '.spr')]:
        name = 'native/csol/' + filename
        header = source / name
        values = re.findall(r'char ' + symbol + r'\[\]\s*=\s*"([^"]+)"', header.read_text(encoding='utf-8-sig'))
        if len(values) != 1:
            raise ValueError(f'Incomplete frozen-death resource contract: {symbol}')
        resource = resource_name(values[0])
        if not resource.startswith(prefix) or not resource.endswith(suffix):
            raise ValueError(f'Invalid frozen-death resource: {resource}')
        resources.add(resource)
        inputs.append(dict(path=name, sha256=digest(header)))
    return resources, inputs


def skill_contract_sources(source):
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    names = ['native/csol/server/scenario_monster_skills.h',
             'native/csol/scenario_monster_effects.hpp',
             'native/csol/server/scenario_session.cpp']
    if 'CSOLZombieBombModel' in session:
        names.append('native/csol/server/scenario_zombie_bomb.h')
    if 'csol::part_effects::Sprites' in session:
        names.append('native/csol/scenario_part_effects.hpp')
    return names


def skill_contract(source):
    """Read the exact skill precache lists; reject unresolved C++ expressions."""
    headers = [('native/csol/server/scenario_monster_skills.h', ''),
               ('native/csol/scenario_monster_effects.hpp', 'csol::monster_effects::')]
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    if 'CSOLZombieBombModel' in session:
        headers.append(('native/csol/server/scenario_zombie_bomb.h', ''))
    constants = {}
    for name, prefix in headers:
        text = (source / name).read_text(encoding='utf-8-sig')
        for key, value in re.findall(r'inline constexpr const char\*\s+(\w+)\s*=\s*"([^"]+)"', text):
            constants[prefix + key] = value
        if name.endswith('/scenario_zombie_bomb.h'):
            blocks = re.findall(r'\bCSOLZombieBombBounceSounds\s*\{([^}]+)\}', text, re.S)
            if len(blocks) != 1:
                raise ValueError('Missing or ambiguous zombie bomb sound contract')
            for index, token in enumerate(blocks[0].split(',')):
                match = re.fullmatch(r'\s*"([^"]+)"\s*', token)
                if not match:
                    raise ValueError(f'Unresolved zombie bomb sound: {token}')
                constants[f'CSOLZombieBombBounceSounds[{index}]'] = match[1]
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
    if 'CSOLZombieBombEvent' in session:
        value = constants.get('CSOLZombieBombEvent')
        if not value or not value.startswith('events/') or not value.endswith('.sc'):
            raise ValueError('Missing or invalid zombie bomb event contract')
        resources.add(resource_name(value))
    if 'csol::part_effects::Sprites' in session:
        text = (source / 'native/csol/scenario_part_effects.hpp').read_text(encoding='utf-8-sig')
        blocks = re.findall(r'\bSprites\s*\{([^}]+)\}', text, re.S)
        if len(blocks) != 1:
            raise ValueError('Missing or ambiguous part effect sprite contract')
        for token in blocks[0].split(','):
            match = re.fullmatch(r'\s*"([^"]+)"\s*', token)
            if not match:
                raise ValueError(f'Unresolved part effect sprite: {token}')
            name = resource_name(match[1])
            if not name.startswith('sprites/') or not name.endswith('.spr'):
                raise ValueError(f'Invalid part effect sprite: {name}')
            resources.add(name)
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


def elite_contract(source):
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    if 'MakeEliteMonsterType' not in session:
        return set()
    # The synthetic type is not an effective row in the map CSV. Resolve its
    # real appearance through the same source catalog used by model precache.
    catalog = (source / 'native/csol/scenario/monster_appearances.cpp').read_text(encoding='utf-8-sig')
    matches = re.findall(r'\{\d+,\s*"zbs_elite_01",(.*?)(?=\n\s*\{\d+,)', catalog, re.S)
    if len(matches) != 1:
        raise ValueError('Missing or ambiguous synthetic elite model')
    appearance = re.findall(r'"([^"]*)"', matches[0])
    if len(appearance) not in (2, 3) or not appearance[0].startswith('models/'):
        raise ValueError('Incomplete synthetic elite appearance/sound contract')
    resources = {resource_name(appearance[0])}
    resources.update(resource_name('sound/' + name) for name in appearance[1:] if name)
    header = (source / 'native/csol/elite_visual_resources.hpp').read_text(encoding='utf-8-sig')
    for kind in ('Models', 'Sounds'):
        block = re.findall(r'\b' + kind + r'\s*\{([^}]+)\}', header, re.S)
        if len(block) != 1:
            raise ValueError(f'Missing elite visual {kind} contract')
        for token in block[0].split(','):
            match = re.fullmatch(r'\s*"([^"]+)"\s*', token)
            if not match:
                raise ValueError(f'Unresolved elite resource: {token}')
            resources.add(resource_name(match[1]))
    return resources


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


def champion_contract(source, common, reader, wave):
    session = (source / 'native/csol/server/scenario_session.cpp').read_text(encoding='utf-8-sig')
    if 'csol::scenario::LoadChampions(' not in session:
        return set(), None
    if reader is None or wave is None:
        raise ValueError('This DLL needs --champion-reader and --wave-resource for all difficulty appearances')
    match = re.fullmatch(r'maps/zbs_wave_([A-Za-z0-9_-]{1,63})\.cso', resource_name(wave))
    if not match:
        raise ValueError('Champion closure requires this map\'s exact wave resource')
    name = 'maps/ZBS_Champ_ZT_' + match[1] + '.cso'
    path = common / name
    if not path.is_file() or path.stat().st_size == 0:
        name = 'maps/ZBS_Champ_ZT_Default.cso'
        path = common / name
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError(f'Linked champion source: {part}')
    path.resolve(strict=True).relative_to(common)
    data = path.read_bytes()
    if not data or len(data) > 8 * 1024 * 1024:
        raise ValueError('Invalid champion resource size')
    result = subprocess.run([str(reader.resolve(strict=True))], input=data, check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    paths = {resource_name(line) for line in result.stdout.decode('utf-8').splitlines()}
    if any(not ((p.startswith('models/') and p.endswith('.mdl')) or
                (p.startswith('sound/') and p.endswith('.wav'))) for p in paths):
        raise ValueError('Unexpected champion appearance resource')
    return paths, dict(table=name, sha256=digest(path), readerSha256=digest(reader),
                       resources=sorted(paths), allDifficulties=True)


def respawn_ui_contract(source):
    """Collect the exact shared HUD list consumed by this server version."""
    session = source / 'native/csol/server/scenario_session.cpp'
    if '#include "../scenario_respawn_resources.hpp"' not in session.read_text(encoding='utf-8-sig'):
        return set(), None
    header = source / 'native/csol/scenario_respawn_resources.hpp'
    text = header.read_text(encoding='utf-8-sig')
    resources = {resource_name(name) for name in re.findall(
        r'"((?:resource|sprites|sound)/[^"\r\n]+\.(?:tga|spr|txt|wav))"', text)}
    if not resources:
        raise ValueError('Empty scenario revival HUD resource contract')
    return resources, dict(resources=sorted(resources), headerSha256=digest(header))


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
    parser.add_argument('--champion-reader', type=Path,
                        help='Matching csol_scenario_champion_resources executable')
    parser.add_argument('--wave-resource',
                        help='Exact encrypted wave filename from this map catalog, relative to common')
    parser.add_argument('--wall-scheme', type=Path,
                        help='Output directory of Prepare-CSOLWallScheme.py; reusable from the previous plan')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    common = args.common.resolve(strict=True)
    source_root = args.source_root.resolve(strict=True) if args.source_root else root
    header = source_root / 'native/csol/server/scenario_monster_resources.h'
    boss = boss_contract(header)
    smoke_event = smoke_event_contract(source_root)
    dialogue, dialogue_info = dialogue_contract(source_root, common, args.dialogue_reader, args.wave_resource)
    respawn_ui, respawn_ui_info = respawn_ui_contract(source_root)
    champions, champion_info = champion_contract(source_root, common, args.champion_reader, args.wave_resource)
    catalog = command(args.catalog_reader, '--catalog', common)
    boss_models = select_boss_models(catalog, boss[0], boss[1]) if boss else []
    if boss_models:
        boss_models = sorted(name for name in boss[2] if name.endswith('.mdl'))
    models = boss_models.copy()
    entries = command(args.collector, common, '--relocatable-json', *models) if models else []
    actual = {row['path'] for row in entries}
    expected = boss[2] if boss else set()
    if models and actual != expected:
        raise ValueError(f'Boss precheck closure drift: new={actual-expected}, stale={expected-actual}')
    hints, hint_contract = boss_hint_contract(source_root) if boss_models else (set(), None)
    entries += [{'path': name} for name in sorted(hints)]
    attacks, attack_contract = boss_attack_contract(source_root) if boss_models else (set(), None)
    attack_models = sorted(name for name in attacks if name.endswith('.mdl'))
    attack_entries = command(args.collector, common, '--relocatable-json', *attack_models) if attack_models else []
    attack_closure = {row['path'] for row in attack_entries} | attacks
    if not set(attack_models).issubset({row['path'] for row in attack_entries}):
        raise ValueError('Collector omitted an Oberon attack model')
    entries += attack_entries + [{'path': name} for name in sorted(attacks) if not name.endswith('.mdl')]
    models += attack_models
    elite = elite_contract(source_root)
    frozen, frozen_inputs = frozen_death_contract(source_root)
    wall_upgrade, wall_upgrade_input = wall_upgrade_contract(source_root)
    skills = skill_contract(source_root) | elite | frozen | champions | wall_upgrade
    skill_models = sorted(name for name in skills if name.endswith('.mdl'))
    skill_entries = command(args.collector, common, '--relocatable-json', *skill_models) if skill_models else []
    if not set(skill_models).issubset({row['path'] for row in skill_entries}):
        raise ValueError('Collector omitted a monster skill model')
    entries += skill_entries + [{'path': name} for name in sorted(skills) if not name.endswith('.mdl')]
    entries += [{'path': name} for name in sorted(dialogue)]
    entries += [{'path': name} for name in sorted(respawn_ui)]
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
        derived_wall_scheme = name == 'resource/csol/wall/v1/WallUpgradeScheme.res'
        source = (wall_scheme_source(args.wall_scheme, rows) if derived_wall_scheme
                  else common / entry.get('source', name))
        if not derived_wall_scheme:
            source.resolve(strict=True).relative_to(common)
        for part in [source, *source.parents]:
            if part.is_symlink() or part.is_junction():
                raise ValueError(f'Linked source path: {part}')
        row = dict(source=str(source), destination=name, bytes=source.stat().st_size,
                   sha256=digest(source),
                   reuse='verified-wall-scheme-v1' if derived_wall_scheme else 'verified-common')
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
        result['bossClosure'] = dict(typeId=boss[0], roots=boss_models + attack_models,
            resources=sorted(actual | attack_closure | hints))
    if hint_contract:
        result['bossHintClosure'] = dict(resources=sorted(hints), contract=hint_contract)
    if attack_contract:
        result['bossAttackClosure'] = dict(roots=sorted(attacks), resources=sorted(attack_closure),
            contract=attack_contract)
    if smoke_event:
        result['smokeEvent'] = dict(path=smoke_event[0],
            headerSha256=digest(source_root / 'native/csol/scenario_smoke.hpp'),
            scriptSha256=digest(smoke_event[1]))
    if dialogue_info:
        result['dialogueClosure'] = dialogue_info
    if respawn_ui_info:
        result['respawnUiClosure'] = respawn_ui_info
    if champion_info:
        result['championClosure'] = champion_info
    if elite:
        result['eliteClosure'] = dict(roots=sorted(elite),
            headerSha256=digest(source_root / 'native/csol/elite_visual_resources.hpp'),
            appearanceCatalogSha256=digest(source_root / 'native/csol/scenario/monster_appearances.cpp'))
    if frozen:
        result['frozenDeathClosure'] = dict(roots=sorted(frozen), sources=frozen_inputs)
    if wall_upgrade:
        result['wallUpgradeClosure'] = dict(roots=sorted(wall_upgrade), contract=wall_upgrade_input)
    args.output.mkdir(parents=True, exist_ok=False)
    def save(name, data):
        (args.output / name).write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    save('resource-plan.json', result)
    closure = command(args.catalog_reader, '--plan', args.output / 'resource-plan.json')
    save('runtime-closure.json', closure)
    save('manifest.json', dict(models=result['models'], effectiveTypes=catalog['effectiveTypes'],
        files=result['files'], added=added, headerSha256=digest(header), bossContractAvailable=boss is not None,
        skillContractSources=[dict(path=name, sha256=digest(source_root / name))
            for name in skill_contract_sources(source_root)],
        previousPlanSha256=digest(args.previous_plan), collectorSha256=digest(args.collector),
        catalogReaderSha256=digest(args.catalog_reader), physicalCopies=0, deployment=False))
    print(json.dumps(dict(files=len(rows), added=added, models=len(result['models']),
                         runtimeDependencies=closure['uniqueDependencyFiles']), ensure_ascii=False))


if __name__ == '__main__':
    main()
