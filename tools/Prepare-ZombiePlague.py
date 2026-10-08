"""Compile the supplied ZP 5.0.8a sources and stage only their required media."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import urllib.request
import zipfile
import importlib.util

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'external/ZombiePlague-5.0.8a'
WORKING = ROOT / 'amxx/zombie_plague'
OUT = ROOT / 'dist/zombieplague'
ARCHIVE_SHA = '883f0988e7beb86cfec41487a2722925b98e2df29268e70770cd0cd9e093ad7c'
MEDIA_REPO = 'mostten/ZombiePlague-for-CS1.6'
MEDIA_COMMIT = '98e92a6205ef94e3bf59860a0831298d8ac78c80'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def destination(path):
    path = path.absolute()
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError(f'Destination escapes workspace: {path}')
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError(f'Reparse destination: {parent}')
        if parent == ROOT:
            break
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def extract(archive):
    if digest(archive) != ARCHIVE_SHA:
        raise ValueError('Archive is not the user-supplied, pinned ZP 5.0.8a package')
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if entry.is_dir():
                continue
            rel = PurePosixPath(entry.filename)
            if rel.is_absolute() or '..' in rel.parts or rel.suffix not in ('.sma', '.inc', '.ini', '.cfg', '.txt'):
                raise ValueError(f'Unexpected archive entry: {entry.filename}')
            path = destination(SOURCE / rel)
            data = z.read(entry)
            if path.exists() and path.read_bytes() != data:
                raise ValueError(f'Preserving modified source: {path}')
            path.write_bytes(data)


def source_category(name):
    if name.endswith('_api'):
        return 'api'
    if name.startswith('zp50_admin_'):
        return 'admin'
    if name.startswith('zp50_class_'):
        return 'classes'
    if name.startswith('zp50_gamemode'):
        return 'modes'
    if name.startswith(('zp50_item', 'zp50_reward', 'zp50_ammopacks')):
        return 'items'
    if 'menu' in name or name == 'zp50_hud_info':
        return 'menus'
    if name.startswith(('zp50_effect', 'zp50_ambience')) or name in ('zp50_zombie_sounds', 'zp50_flashlight', 'zp50_nightvision'):
        return 'effects'
    if name.startswith(('zp50_grenade_', 'zp50_weapon_', 'zp50_human_ammo', 'zp50_human_armor')):
        return 'weapons'
    return 'core'


def import_editable(seed, working, key, previous, imported):
    working = destination(working)
    seed_hash = digest(seed)
    if not working.exists() or digest(working) == previous.get(key, {}).get('seedSha256'):
        working.write_bytes(seed.read_bytes())
    elif digest(working) != seed_hash and key not in previous:
        raise ValueError(f'Preserving an unregistered source with the same name: {working.name}')
    imported[key] = {'seed': seed.relative_to(ROOT).as_posix(), 'seedSha256': seed_hash,
                     'source': working.relative_to(ROOT).as_posix(), 'sourceSha256': digest(working)}
    return working


def compile_plugins():
    # Keep the complete editable Mod together, including its headers, language
    # bases, configuration and author/license notices. Third-party SDKs remain
    # pinned dependencies; compiled files never enter this source tree.
    ledger_path = destination(ROOT / 'build/amxx/imported-sources.json')
    previous = json.loads(ledger_path.read_text(encoding='utf-8')) if ledger_path.exists() else {}
    imported, sources = {}, {}
    support = (
        ('addons/amxmodx/scripting/include', 'include', ('.inc',)),
        ('addons/amxmodx/configs', 'configs', ('.cfg', '.ini')),
        ('addons/amxmodx/data/lang', 'lang', ('.txt',)),
    )
    for source_folder, category, extensions in support:
        origin = SOURCE / source_folder
        for seed in sorted(origin.rglob('*')):
            if seed.is_file() and seed.suffix in extensions:
                relative = Path(category) / seed.relative_to(origin)
                import_editable(seed, WORKING / relative, 'support/' + relative.as_posix(), previous, imported)
    for seed in sorted(SOURCE.glob('*.txt')):
        import_editable(seed, WORKING / 'docs' / seed.name, 'support/docs/' + seed.name, previous, imported)
    spec = importlib.util.spec_from_file_location('zombie_localization', ROOT / 'tools/Localize-ZombiePlague.py')
    localization = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(localization)
    translated_sources, _, _ = localization.prepare()
    scripting = SOURCE / 'addons/amxmodx/scripting'
    compiler = ROOT / '.tools/amxx-1.9.0.5303/addons/amxmodx/scripting/amxxpc.exe'
    load_list = WORKING / 'configs/plugins-zp50_ammopacks.ini'
    plugins = re.findall(r'^([a-z0-9_]+)\.amxx', load_list.read_text(), re.M)
    # Root amxx is the editable source; pinned seeds stay outside that folder.
    for name in plugins:
        files = list(scripting.rglob(name + '.sma'))
        if len(files) != 1:
            raise ValueError(f'Ambiguous or absent plugin source: {name}')
        seed = translated_sources.get(name, files[0])
        sources[name] = import_editable(seed, WORKING / source_category(name) / (name + '.sma'),
                                        name, previous, imported)
    spec = importlib.util.spec_from_file_location('zombie_reapi_patch', ROOT / 'tools/Apply-ZombiePlaguePatch.py')
    migration = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(migration)
    patch_result = migration.apply_migration(WORKING)
    for entry in imported.values():
        entry['sourceSha256'] = digest(ROOT / entry['source'])
    ledger_path.write_text(json.dumps(imported, indent=2) + '\n', encoding='utf-8')
    destination(ROOT / 'build/amxx/zp-reapi-patch.json').write_text(json.dumps(patch_result, indent=2) + '\n', encoding='utf-8')
    compiled = []
    log = destination(ROOT / 'build/logs/zombieplague-build.log')
    with log.open('w', encoding='utf-8') as output:
        for name in plugins:
            target = destination(ROOT / 'build/amxx/plugins' / (name + '.amxx'))
            plugin_source = sources[name]
            result = subprocess.run([str(compiler), str(plugin_source), '-i' + str(WORKING / 'include'),
                                     '-i' + str(ROOT / '.tools/reapi-5.29.0.358/addons/amxmodx/scripting/include'),
                                     '-i' + str(compiler.parent / 'include'), '-o' + str(target)],
                                    cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
            if result.returncode or not target.is_file():
                raise RuntimeError(f'Pawn compilation failed: {name}; see {log}')
            compiled.append({'name': name, 'sha256': digest(target), 'sourceSha256': digest(plugin_source),
                             'source': plugin_source.relative_to(ROOT).as_posix(),
                             'artifact': target.relative_to(ROOT).as_posix()})
    return compiled


def stage_media(map_name):
    game = ROOT / 'sandbox/cs-server/Half-Life'
    amxx = WORKING
    text = '\n'.join(p.read_text(errors='replace') for p in amxx.rglob('*') if p.suffix in ('.ini', '.sma'))
    required = set(re.findall(r'(?:models|sprites)/[\w/+.\-]+\.(?:mdl|spr)', text))
    required.update('sound/' + p for p in re.findall(r'[\w/+.\-]+\.(?:wav|mp3)', text) if '/' in p)
    required.add('models/player/zombie_source/zombie_source.mdl')
    ini = (amxx / 'configs/zombieplague.ini').read_text()
    player_section = re.search(r'\[Player Models\](.*?)(?=\n\[)', ini, re.S).group(1)
    for line in player_section.splitlines():
        if '=' in line and not line.lstrip().startswith(';'):
            required.update(f'models/player/{p.strip()}/{p.strip()}.mdl' for p in line.split('=', 1)[1].split(','))
    bsp = game / 'cstrike/maps' / (map_name + '.bsp')
    b = bsp.read_bytes()
    start, size = struct.unpack_from('<ii', b, 4)
    entities = [dict(re.findall(r'"([^"]*)"\s*"([^"]*)"', e))
                for e in re.findall(r'\{(.*?)\}', b[start:start+size].decode('latin1'), re.S)]
    # BSP authors often embed their own drive/path. Engines load the WAD by
    # basename from the game search path; never use that build-machine path.
    required.update(PurePosixPath(p.replace('\\', '/')).name for p in entities[0].get('wad', '').split(';') if p)
    for entity in entities:
        for value in entity.values():
            if value.lower().endswith(('.mdl', '.spr')):
                required.add(value)
            elif value.lower().endswith(('.wav', '.mp3')):
                required.add('sound/' + value)
    for sky in {entities[0].get('skyname', ''), 'space'} - {''}:
        required.update('gfx/env/' + sky + face + '.tga' for face in ('bk', 'dn', 'ft', 'lf', 'rt', 'up'))
    baseline = json.loads((ROOT / 'analysis/installation/original-baseline.json').read_text('utf-8-sig'))
    original = Path(baseline['source'])
    lookup = {e['path'].replace('\\', '/').lower(): e for e in baseline['files']}
    tree = None
    records = []
    for rel in sorted(required):
        if PurePosixPath(rel).is_absolute() or '..' in PurePosixPath(rel).parts or ':' in rel or '\\' in rel:
            raise ValueError(f'Unsafe resource path: {rel}')
        source = next((game / folder / rel for folder in ('cstrike', 'valve') if (game / folder / rel).is_file()), None)
        origin = 'existing-sandbox'
        if source is None:
            for folder in ('cstrike', 'cstrike_downloads', 'valve', ''):
                key = (folder + '/' + rel).strip('/').lower()
                if key not in lookup:
                    continue
                entry = lookup[key]
                source = original / entry['path']
                if digest(source).lower() != entry['sha256'].lower():
                    raise ValueError(f'Protected-original baseline mismatch: {rel}')
                origin = 'original-baseline:' + key
                break
        target = destination(OUT / 'media' / rel)
        if source is not None:
            shutil.copyfile(source, target)
        else:
            if tree is None:
                request = urllib.request.Request(f'https://api.github.com/repos/{MEDIA_REPO}/git/trees/{MEDIA_COMMIT}?recursive=1',
                                                 headers={'User-Agent': 'GoldCraft-build'})
                tree = {e['path']: e for e in json.load(urllib.request.urlopen(request, timeout=30))['tree']}
            entry = tree.get(rel)
            if entry is None or entry['type'] != 'blob':
                raise FileNotFoundError(f'Required resource missing: {rel}')
            url = f'https://raw.githubusercontent.com/{MEDIA_REPO}/{MEDIA_COMMIT}/{rel}'
            data = urllib.request.urlopen(url, timeout=30).read()
            blob_hash = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            if blob_hash != entry['sha']:
                raise ValueError(f'Pinned Git blob mismatch: {rel}')
            target.write_bytes(data)
            origin = url
        records.append({'path': rel, 'bytes': target.stat().st_size, 'sha256': digest(target), 'source': origin})
    return {'map': map_name, 'bspSha256': digest(bsp), 'resources': records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--map', default='cs_assault')
    parser.add_argument('--compile-only', action='store_true', help='Keep existing staged media and rebuild editable Pawn sources only')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_]{1,31}', args.map):
        parser.error('Invalid SyPB map name')
    if args.archive:
        extract(args.archive)
    plugins = compile_plugins()
    if args.compile_only:
        previous_manifest = OUT / 'manifest.json'
        result = json.loads(previous_manifest.read_text(encoding='utf-8')) if previous_manifest.exists() else {'resources': []}
    else:
        result = stage_media(args.map)
    result.update({'archiveSha256': ARCHIVE_SHA, 'amxx': '1.9.0.5303', 'reapi': '5.29.0.358', 'plugins': plugins,
                   'mediaRepository': MEDIA_REPO, 'mediaCommit': MEDIA_COMMIT})
    destination(OUT / 'manifest.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(f"Compiled {len(plugins)} ZP/ReAPI plugins; {'retained' if args.compile_only else 'staged'} {len(result['resources'])} required resources.")


if __name__ == '__main__':
    main()
