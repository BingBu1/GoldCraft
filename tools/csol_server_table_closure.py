"""Audit a scenario server's data closure against a baseline plus update ZIP.

Only reads the selected frozen source, common resources and deployed resources.
Produces package input rows; it does not copy, deploy, compile or start a server.
File coverage is not a replacement for the production parser or gameplay tests.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import stat
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.exists() and (part.is_symlink() or
                getattr(part.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise ValueError('Reparse point is not a resource source: ' + str(part))
    return path


def relative(name):
    if (not isinstance(name, str) or not name or any(c in name for c in '\\:<>"|?*') or
            any(part in ('', '.', '..') or part.endswith(('.', ' ')) for part in name.split('/')) or
            any(ord(c) < 32 or ord(c) == 127 for c in name)):
        raise ValueError('Invalid relative resource: ' + repr(name))
    return name


def identity(path):
    path = plain(path)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest(path))


def literal(source, name, token, evidence):
    path = source / name
    text = path.read_text('utf-8-sig')
    if token not in text:
        raise ValueError('Frozen source contract changed: ' + name + ' / ' + token)
    evidence[str(path)] = identity(path)
    return text


def collect(source, deployed, common, map_name, dependency_lists=()):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,63}', map_name):
        raise ValueError('Invalid map basename')
    source, deployed, common = map(plain, (source, deployed, common))
    rows, sources, absent = {}, {}, []

    def add(name, group, source_name=None, token=None, common_required=False, optional=False):
        name = relative(name)
        if source_name:
            literal(source, source_name, token or name, sources)
        path, original = deployed / name, common / name
        if optional and not path.exists() and not original.exists():
            absent.append(name)
            return
        if not path.is_file():
            raise ValueError('Verified local deployment is incomplete: ' + name)
        local = identity(path)
        common_id = identity(original) if original.is_file() else None
        matches = common_id is not None and local['sha256'] == common_id['sha256'] and local['bytes'] == common_id['bytes']
        if common_required and not matches:
            raise ValueError('Server table differs from common: ' + name)
        if not local['bytes']:
            raise ValueError('Empty required resource: ' + name)
        key = name.lower()
        if key in rows:
            if rows[key]['resource'] != name:
                raise ValueError('Case-colliding resource: ' + name)
            if group not in rows[key]['groups']:
                rows[key]['groups'].append(group)
            return
        rows[key] = dict(resource=name, groups=[group], source=common_id['path'] if matches else local['path'],
                         bytes=local['bytes'], sha256=local['sha256'], deployed=local,
                         common=common_id, commonMatchesDeployed=matches)

    scenario = 'native/csol/scenario/'
    direct = [
        ('resource/maplist.csv', 'map_resources.cpp'),
        ('maps/ZBS_DropItem.cso', 'drop_items.cpp'),
        ('resource/ZBSVER2/ZBS_neededexp.cso', 'upgrade_tables.cpp'),
        ('resource/ZBSVER2/ZBS_class.cso', 'upgrade_tables.cpp'),
        ('resource/buff/BuffWeapon.cso', 'equipment_resources.cpp'),
        ('maps/ZBS.json', 'equipment_resources.cpp'),
        ('resource/buff/ClassMod.csv', 'equipment_resources.cpp'),
        ('resource/buff/ClassModConfig.json', 'equipment_resources.cpp'),
        ('resource/buff/ClassModPreset.csv', 'equipment_resources.cpp'),
        ('resource/AnnounceRibbon/announceribbon.cso', 'announce.cpp'),
        ('resource/AnnounceRibbon/AnnounceCategory.cso', 'announce.cpp'),
        ('resource/FloatingDamage/FloatingDamageInfo.jsoo', 'damage_display_modes.hpp'),
        ('resource/item.csv', '../server/scenario_weapon_state.cpp'),
        ('resource/defaultitems.csv', '../server/scenario_weapon_state.cpp'),
        ('classes2/emotion.csv', '../server/scenario_character_skills/host.cpp'),
        ('resource/ModeEvent/modeevent.csv', '../server/scenario_character_skills/host_notifications.cpp'),
        ('sound/sentences.json', '../server/scenario_character_skills/host_notifications.cpp'),
    ]
    for name, owner in direct:
        add(name, 'server-tables', scenario + owner, common_required=True)
    for prefix, owner, optional in [
            ('zbs_wave_', 'map_resources.cpp', False), ('zbs_', 'map_resources.cpp', False),
            ('ZBS_ZT_', 'map_resources.cpp', False), ('ZBS_TRG_', 'map_resources.cpp', False),
            ('ZBS_DIE_EVENT_', 'death_rules.cpp', True), ('zbs_sk_', 'boss_skills.cpp', True)]:
        add('maps/' + prefix + map_name + '.cso', 'server-tables', scenario + owner,
            '"maps/' + prefix + '"', common_required=True, optional=optional)
    difficulty = 'maps/ZBS_Difficulty_' + map_name + '.cso'
    literal(source, scenario + 'difficulty.cpp', '"maps/ZBS_Difficulty_"', sources)
    if not (common / difficulty).exists() and not (deployed / difficulty).exists():
        absent.append(difficulty)
        difficulty = 'maps/ZBS_Difficulty_Default.cso'
    add(difficulty, 'server-tables', common_required=True)
    add('maps/ZBS_GoldenZombie_dedi.json', 'server-tables', scenario + 'golden.cpp', common_required=True, optional=True)
    for symbol in ('MessagePath', 'LocalizationPath'):
        text = literal(source, 'native/csol/scenario_round_hud.hpp', symbol, sources)
        names = re.findall(r'\b' + symbol + r'\[\]\s*=\s*"([^"]+)"', text)
        if len(names) != 1:
            raise ValueError('Ambiguous scenario HUD resource: ' + symbol)
        add(names[0], 'server-tables', common_required=True)
    for suffix, owner in [('.bsp', 'scenario_session.cpp'), ('.nav', 'scenario_navigation.cpp')]:
        add('maps/' + map_name + suffix, 'map', 'native/csol/server/' + owner, '"' + suffix + '"', common_required=True)

    # The deployed manifest is the actual CSOL_PrecacheCharacters input. Its
    # contents include SEQ/texture companions, converted models and UI assets.
    for name in ('models/csol/characters.json', 'shader/csol/characters.txt'):
        add(name, 'characters', 'native/csol/server/characters.cpp')
    add('models/csol/classmenu2.json', 'characters', 'native/csol/client/characters.cpp')
    manifest = (deployed / 'shader/csol/characters.txt').read_text('utf-8-sig').splitlines()
    if len(manifest) != len(set(manifest)) or len(manifest) > 4096:
        raise ValueError('Duplicate or oversized character manifest')
    for name in manifest:
        if name:
            add(name, 'characters')

    text = literal(source, 'native/csol/buy_menu.hpp', 'PresentationResource', sources)
    names = re.findall(r'PresentationResource\[\]\s*=\s*"([^"]+)"', text)
    if len(names) != 1:
        raise ValueError('Ambiguous buy menu presentation resource')
    add(names[0], 'buy-menu')
    presentation = json.loads((deployed / names[0]).read_text('utf-8-sig'))

    def textures(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key == 'path':
                    yield child
                else:
                    yield from textures(child)
        elif isinstance(value, list):
            for child in value:
                yield from textures(child)
    for name in textures(presentation):
        if not isinstance(name, str) or not name.startswith('gfx/csol/buy/') or not name.endswith('.tga'):
            raise ValueError('Unexpected buy-menu texture')
        add(name, 'buy-menu')
    literal(source, 'native/csol/buy_menu.cpp', 'std::vector<std::string> Resources', sources)

    # These files hold reviewed explicit precache lists. Enumerate all exact
    # literals, including damage JSON and event scripts required by K3 spawn.
    for name in ('native/csol/server/wpn_k3.cpp', 'native/csol/server/wpn_cannon.cpp',
                 'native/csol/cannon_resources.hpp', 'native/csol/beamgun_resources.hpp',
                 'native/csol/server/wpn_tknifeex2.cpp'):
        text = literal(source, name, '"', sources)
        for resource in set(re.findall(r'"((?:models|sprites|sound|events|csol|weapons)/[^"\r\n]+\.(?:mdl|spr|wav|txt|sc|json|tga|webm))"', text)):
            if resource.startswith('weapons/'):
                resource = 'sound/' + resource
            add(resource, 'weapons')
    for path in dependency_lists:
        document = json.loads(plain(path).read_text('utf-8-sig'))
        if not isinstance(document, list) or not document:
            raise ValueError('Expected a nonempty production model dependency list')
        sources[str(path)] = identity(path)
        for row in document:
            if not isinstance(row, dict) or 'path' not in row:
                raise ValueError('Invalid model dependency record')
            add(row['path'], 'weapon-model-dependencies')
    return dict(resources=sorted(rows.values(), key=lambda r: r['resource'].lower()),
                sourceEvidence=list(sources.values()), optionalAbsent=absent)


def archive_index(path):
    path = plain(path)
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        manifests = [name for name in names if name.endswith('/update-manifest.json')]
        if len(manifests) != 1:
            raise ValueError('Expected one update manifest')
        manifest = json.loads(archive.read(manifests[0]))
        prefix = manifests[0].removesuffix('update-manifest.json')
        result = {}
        legacy_manifest = any(row['path'].startswith('payload/') for row in manifest['files'])
        for row in manifest['files']:
            # The 10/24 manifest also hashes installer scripts beside payload.
            if legacy_manifest and not row['path'].startswith('payload/'):
                continue
            name = relative(row['path'].removeprefix('payload/'))
            if name.lower() in result:
                raise ValueError('Duplicate archive destination: ' + name)
            member = prefix + 'payload/' + name
            if member not in names:
                raise ValueError('Manifest entry absent from ZIP: ' + name)
            result[name.lower()] = dict(**row, archive=str(path), member=member)
        return result, manifest.get('requiredBaseline', [])


def audit(closure, archives):
    merged, archive_evidence, requirements = {}, [], []
    for path in archives:
        rows, required = archive_index(path)
        merged.update(rows)
        requirements.extend(required)
        archive_evidence.append(identity(path))
    for row in requirements:
        old = merged.get(row['path'].lower())
        if not old or old['sha256'] != row['sha256'] or old['bytes'] != row['bytes']:
            raise ValueError('Required baseline identity is unproven: ' + row['path'])
    additions, unchanged, differences = [], [], []
    for row in closure['resources']:
        name = 'cs/cstrike/' + row['resource']
        prior = merged.get(name.lower())
        matched = prior and prior['sha256'] == row['sha256'] and prior['bytes'] == row['bytes']
        entry = {key: row[key] for key in ('source', 'bytes', 'sha256')}
        entry['path'] = name
        if matched:
            unchanged.append(entry)
        else:
            additions.append(entry)
            differences.append(dict(path=name, groups=row['groups'], reason='changed' if prior else 'missing',
                                    previousSha256=prior['sha256'] if prior else None, sha256=row['sha256']))
    # Verify actual compressed payload bytes as well as the metadata used to
    # decide omission. Missing resources never count as an implicit base file.
    verified = []
    by_archive = {}
    for row in unchanged:
        prior = merged[row['path'].lower()]
        by_archive.setdefault(prior['archive'], []).append((row, prior))
    for path, entries in by_archive.items():
        with zipfile.ZipFile(path) as archive:
            for row, prior in entries:
                with archive.open(prior['member']) as stream:
                    actual = hashlib.file_digest(stream, 'sha256').hexdigest()
                if actual != row['sha256']:
                    raise ValueError('Archive resource bytes do not match manifest: ' + row['path'])
                verified.append(row['path'])
    return dict(schema=1, files=additions, filesCount=len(additions), bytes=sum(r['bytes'] for r in additions),
                unchanged=unchanged, differences=differences, archiveEvidence=archive_evidence,
                verifiedArchiveResources=verified, closure=closure,
                boundaries=dict(fileCoverageOnly=True, productionParserExecuted=False, compiled=False,
                                runtimeModified=False, remoteDeployed=False, liveGameplay=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--deployed', type=Path, required=True)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--map', default='zs_deadend')
    parser.add_argument('--archive', type=Path, action='append', required=True,
                        help='Oldest baseline first, then updates in installation order')
    parser.add_argument('--dependency-list', type=Path, action='append', default=[],
                        help='JSON output from the matching production csol_resources --relocatable-json reader')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Use a new report path')
    closure = collect(args.source_root, args.deployed, args.common, args.map, args.dependency_list)
    report = audit(closure, args.archive)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    print(json.dumps(dict(resources=len(closure['resources']), additions=report['filesCount'],
                          bytes=report['bytes'], report=identity(args.output)), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
