"""Package a verified client runtime with the adapted official MetaHook GUI.

Planning only reads runtime files. Packaging requires --output and never installs
into a game. Local plans include source paths; distributable manifests do not.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

HERE = Path(__file__).resolve().parent
PLUGINS = (
    'VGUI2Extension.dll', 'CaptionMod.dll', 'Renderer_AVX2.dll', 'GoldCraft.dll',
    'BulletPhysics.dll', 'StudioEvents.dll', 'PrecacheManager.dll', 'HeapPatch.dll',
    'ResourceReplacer.dll', 'InterpFix.dll',
)
# Only plugin resources, never CS game content, user configs, logs or pairing data.
TREES = {
    'metahook/dlls': {'.dll'}, 'metahook/gamedata': {'.json'},
    'metahook/shaders': {'.glsl', '.fsh', '.vsh', '.h'},
    'renderer/shader': {'.glsl', '.fsh', '.vsh', '.h'},
    'renderer/texture': {'.dds', '.tga', '.png'},
    'captionmod': {'.txt', '.json', '.csv', '.res', '.ttf', '.otf', '.tga', '.png'},
    'bulletphysics': {'.txt', '.json', '.cfg', '.res'},
    'studioevents': {'.txt', '.json'}, 'vgui2ext': {'.txt', '.json', '.res', '.ttf', '.otf'},
}
# Renderer loads these files outside its shader/texture trees. Keep an exact
# list: recursively copying renderer/maps/models would also ship profiling
# output, game assets and Blender source files from the development runtime.
RESOURCE_FILES = (
    'renderer/decal_textures.txt', 'renderer/default_entity.txt',
    'renderer/gameui_english.txt', 'renderer/gameui_koreana.txt',
    'renderer/gameui_schinese.txt', 'renderer/OptionsSubVideoAdvancedDlg.res',
    'maps/cs_assault_entity.txt', 'maps/cs_assault_shadow.obj',
    'maps/cs_assault_shadow.mtl', 'maps/de_dust2_entity.txt',
    'maps/de_dust2_shadow.obj',
    'sprites/laserbeam_external.txt', 'sprites/laserbeam_Frame_0.hdr',
    'sprites/radio_external.txt', 'sprites/voiceicon_external.txt',
    'models/player/terror/terror_external.txt',
    'models/player/terror/terror_physics.txt',
)
HELPERS = ('Install-GoldCraft.cmd', 'Install-GoldCraft.ps1', 'Common.ps1', 'README.md', 'upstream.json')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.exists() and (part.is_symlink() or getattr(part.lstat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError(f'Reparse point is not a package source/destination: {part}')
    return path


def relative_name(name):
    if not name or '\\' in name or ':' in name or any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError(f'Unsafe package path: {name}')
    return name


def verified_gui(directory):
    directory = safe(directory)
    identity = json.loads((directory / 'gui-build.json').read_text('utf-8'))
    pin = json.loads((HERE / 'gui-source.json').read_text('utf-8'))
    expected = dict(upstreamCommit=pin['commit'], patchSha256=digest(HERE / 'goldcraft-gui.patch'),
                    integrationSha256=digest(HERE / 'GoldCraftIntegration.cs'))
    if any(identity.get(key) != value for key, value in expected.items()):
        raise ValueError('GUI was not built from the current pinned integration')
    files = identity.get('files')
    if not isinstance(files, list) or not files:
        raise ValueError('Empty GUI build identity')
    names = set()
    for row in files:
        name = relative_name(row['path'])
        if name.casefold() in names or name == 'gui-build.json':
            raise ValueError('Duplicate/reserved GUI output: ' + name)
        names.add(name.casefold())
        source = safe(directory / name)
        if not source.is_relative_to(directory) or not source.is_file():
            raise ValueError('Invalid GUI output: ' + name)
        if source.stat().st_size != row['bytes'] or digest(source) != row['sha256']:
            raise ValueError('GUI output changed: ' + name)
    if 'metahookinstalleravalonia.desktop.exe' not in names:
        raise ValueError('Missing adapted official GUI executable')
    return identity


def make_plan(game, upstream, license_file, gui):
    game, upstream, license_file = map(safe, (game, upstream, license_file))
    pin = json.loads((HERE / 'upstream.json').read_text('utf-8'))
    cli = upstream / 'MetahookInstallerCLI.exe'
    if digest(cli) != pin['cliSha256']:
        raise ValueError('Official installer differs from pinned release')
    if 'Copyright (c) 2021 hzqst' not in license_file.read_text('utf-8'):
        raise ValueError('Provide the original MIT license from the pinned installer source')
    files = {}

    def add(source, name):
        source = safe(source)
        name = relative_name(name)
        key = name.casefold()
        if key in files:
            raise ValueError(f'Duplicate package destination: {name}')
        if not source.is_file():
            raise FileNotFoundError(source)
        files[key] = dict(source=str(source), path=name, bytes=source.stat().st_size, sha256=digest(source))

    add(cli, 'MetahookInstallerCLI.exe')
    gui_identity = verified_gui(gui)
    # The official Desktop executable is published self-contained. Preserve any
    # sidecar files declared by that build, without accidentally bundling caches.
    for row in gui_identity['files']:
        name = row['path']
        target = 'GoldCraftInstaller.exe' if name == 'MetahookInstallerAvalonia.Desktop.exe' else name
        add(Path(gui) / name, target)
    add(Path(gui) / 'gui-build.json', 'gui-build.json')
    add(license_file, 'licenses/MetahookInstaller-LICENSE.txt')
    for name in HELPERS:
        add(HERE / name, name)
    # The launcher is the project's deployed adaptation. Engine/game DLLs and
    # Steam authentication components are deliberately not payload inputs.
    for name in ('MetaHook.exe', 'SDL2.dll', 'SDL3.dll'):
        add(game / name, 'install/output/' + name)
    for name in PLUGINS:
        add(game / 'cstrike/metahook/plugins' / name, 'install/output/svencoop/metahook/plugins/' + name)
    # MetaHook can prefer this alias. Always supply the same adapted build.
    add(game / 'cstrike/metahook/plugins/BulletPhysics.dll',
        'install/output/svencoop/metahook/plugins/BulletPhysics_AVX2.dll')
    for tree, extensions in TREES.items():
        directory = safe(game / 'cstrike' / tree)
        if not directory.is_dir():
            raise FileNotFoundError(directory)
        for source in sorted(directory.rglob('*')):
            safe(source)
            if not source.is_file():
                continue
            if source.suffix.lower() == '.pdb' or (tree == 'renderer/shader' and source.name.endswith('_cache.txt')):
                continue
            if source.suffix.lower() not in extensions:
                raise ValueError('Review new resource type instead of silently omitting it: ' + str(source))
            add(source, 'install/output/svencoop/' + source.relative_to(game / 'cstrike').as_posix())
    for name in RESOURCE_FILES:
        add(game / 'cstrike' / name, 'install/output/svencoop/' + name)
    for required in ('metahook/gamedata/index.json', 'renderer/shader/goldcraft.vert.glsl',
                     'renderer/shader/goldcraft.frag.glsl', 'renderer/shader/goldcraft.geom.glsl',
                     'captionmod/gameui_english.txt', 'bulletphysics/gameui_english.txt'):
        if ('install/output/svencoop/' + required).casefold() not in files:
            raise ValueError('Missing paired runtime resource: ' + required)
    repo = HERE.parents[1]
    for source in sorted((repo / 'notices').glob('*.txt')):
        if source.name.casefold() == 'metahookinstaller-license.txt':
            if digest(safe(source)) != digest(license_file):
                raise ValueError('Repository installer notice differs from the pinned installer license')
            continue  # Already included from the explicitly verified license input.
        add(source, 'licenses/' + source.name)
    add(repo / 'THIRD_PARTY_NOTICES.md', 'licenses/THIRD_PARTY_NOTICES.md')
    engine = dict(engineSha256=digest(safe(game / 'hw.dll')),
                  clientSha256=digest(safe(game / 'cstrike/cl_dlls/client.dll')))
    return dict(schema=1, product='GoldCraft MetaHook client plugins', runtimeChannel='deployed-stable', officialInstaller=pin,
                gui=gui_identity, supportedGame=engine, appId=10, modDirectory='cstrike', plugins=list(PLUGINS),
                dllPaths=['FreeImage'], files=sorted(files.values(), key=lambda row: row['path']))


def validate_publish(plan, output):
    output = safe(output)
    archive = output.with_suffix('.zip')
    if archive == output or output.exists() or archive.exists():
        raise ValueError('Use a new output directory; previous packages are preserved')
    if plan.get('schema') != 1 or not isinstance(plan.get('files'), list) or not plan['files']:
        raise ValueError('Invalid package plan schema or empty file list')
    destinations = set()
    sources = []
    for row in plan['files']:
        relative = relative_name(row['path'])
        folded = relative.casefold()
        if folded in destinations or folded == 'package.json':
            raise ValueError('Duplicate or reserved package destination: ' + relative)
        destinations.add(folded)
        destination = safe(output / relative)
        if not destination.is_relative_to(output):
            raise ValueError('Package destination escapes output directory')
        source = safe(row['source'])
        if not source.is_file() or source.is_relative_to(output) or source == archive:
            raise ValueError('Output overlaps a source or source is not a file: ' + relative)
        if not isinstance(row.get('sha256'), str) or len(row['sha256']) != 64 or any(c not in '0123456789abcdef' for c in row['sha256']):
            raise ValueError('Invalid source digest: ' + relative)
        sources.append(source)
    # Validate all path structure before reading payload bytes or writing files.
    for row, source in zip(plan['files'], sources):
        if source.stat().st_size != row['bytes'] or digest(source) != row['sha256']:
            raise ValueError('Source changed after planning: ' + row['path'])
    return output


def publish(plan, output):
    output = validate_publish(plan, output)
    output.mkdir(parents=True)
    for row in plan['files']:
        target = safe(output / row['path'])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(row['source'], target)
        if digest(target) != row['sha256']:
            raise ValueError('Packaged file changed: ' + row['path'])
    manifest = dict(plan, files=[{k: v for k, v in row.items() if k != 'source'} for row in plan['files']])
    (output / 'package.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    archive = output.with_suffix('.zip')
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as stream:
        for path in sorted(output.rglob('*')):
            if path.is_file():
                stream.write(path, output.name + '/' + path.relative_to(output).as_posix())
    with zipfile.ZipFile(archive) as stream:
        if stream.testzip() is not None:
            raise ValueError('ZIP integrity check failed')
        for row in manifest['files']:
            if hashlib.sha256(stream.read(output.name + '/' + row['path'])).hexdigest() != row['sha256']:
                raise ValueError('ZIP member mismatch: ' + row['path'])
    return dict(directory=str(output), zip=str(archive), sha256=digest(archive), files=len(manifest['files']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, help='Verified client game root, read-only input')
    parser.add_argument('--official', type=Path, help='Extracted pinned official release')
    parser.add_argument('--installer-license', type=Path)
    parser.add_argument('--gui', type=Path, help='Verified publish directory from build_gui.py')
    parser.add_argument('--plan', type=Path, help='Local evidence path; contains source paths')
    parser.add_argument('--from-plan', type=Path, help='Package an already reviewed plan, rejecting changed inputs')
    parser.add_argument('--output', type=Path, help='Omit for a read-only plan; provide to build ZIP')
    args = parser.parse_args()
    if args.from_plan:
        if not args.output or any((args.runtime, args.official, args.installer_license, args.plan, args.gui)):
            parser.error('--from-plan requires --output and no new planning arguments')
        print(json.dumps(publish(json.loads(safe(args.from_plan).read_text('utf-8')), args.output)))
        return
    if not all((args.runtime, args.official, args.installer_license, args.plan, args.gui)):
        parser.error('Provide --runtime, --official, --installer-license, --gui and --plan')
    plan_path = safe(args.plan)
    if plan_path.exists():
        raise ValueError('Keep existing plans; select a new evidence file')
    plan = make_plan(args.runtime, args.official, args.installer_license, args.gui)
    plan_path.parent.mkdir(parents=True, exist_ok=True)
    plan_path.write_text(json.dumps(plan, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    result = dict(plannedFiles=len(plan['files']), bytes=sum(row['bytes'] for row in plan['files']), installed=False)
    if args.output:
        result.update(publish(plan, args.output))
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
