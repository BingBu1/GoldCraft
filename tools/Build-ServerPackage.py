"""Build a private, relocatable Windows dual-server ZIP from deployed runtimes.

No sandbox saves, identities or secrets are copied. This is deliberately separate
from the public source allowlist; the ZIP contains licensed game/runtime assets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile
import zlib

ROOT = Path(__file__).resolve().parent.parent
CS = ROOT / 'sandbox/cs-server/Half-Life'
MC = ROOT / 'sandbox/neoforge-server'
TEMPLATES = ROOT / 'tools/server-package'
PIN = json.loads((ROOT / 'sources.lock.json').read_text(encoding='utf-8'))['minecraft']


def safe_file(path: Path) -> Path:
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError(f'Reparse source rejected: {path.name}')
    if not path.is_file():
        raise FileNotFoundError(path)
    return path


def build_plan(steam: Path, native_crt: Path, core_jar: Path):
    files: dict[str, Path | bytes] = {}

    def add(source, name):
        if name in files:
            raise ValueError(f'Duplicate package member: {name}')
        files[name] = safe_file(source)

    def generated(name, text):
        files[name] = text.encode('utf-8')

    def tree(source, target, extensions=None):
        for path in sorted(source.rglob('*')):
            if path.is_file() and (extensions is None or path.suffix.lower() in extensions):
                add(path, target + '/' + path.relative_to(source).as_posix())

    for name in ('hlds.exe', 'swds.dll', 'FileSystem_Stdio.dll', 'steam_api.dll',
                 'tier0.dll', 'vstdlib.dll', 'vgui.dll', 'vgui2.dll', 'SDL2.dll'):
        add(CS / name, 'cs/' + name)
    # SteamAPI can otherwise silently depend on the developer's Steam registry.
    for name in ('steamclient.dll', 'tier0_s.dll', 'vstdlib_s.dll'):
        add(steam / name, 'cs/' + name)
    for source in sorted(native_crt.glob('*.dll')):
        add(source, 'cs/' + source.name)
    if 'cs/vcruntime140.dll' not in files or 'cs/msvcp140.dll' not in files:
        raise ValueError('Provide the x86 Microsoft Visual C++ CRT redistributable directory')
    for game in ('cstrike', 'valve'):
        for directory, suffixes in (
            ('models', {'.mdl'}), ('sound', {'.wav', '.mp3'}),
            ('sprites', {'.spr', '.txt'}), ('events', {'.sc'}), ('gfx/env', {'.tga', '.bmp'})):
            tree(CS / game / directory, f'cs/{game}/{directory}', suffixes)
        for name in ('delta.lst', 'liblist.gam', 'titles.txt', 'steam.inf', 'decals.wad'):
            add(CS / game / name, f'cs/{game}/{name}')
    for name in ('halflife.wad', 'liquids.wad', 'gfx.wad', 'fonts.wad'):
        add(CS / 'valve' / name, 'cs/valve/' + name)
    add(CS / 'cstrike/cs_assault.wad', 'cs/cstrike/cs_assault.wad')
    add(CS / 'cstrike/dlls/mp.dll', 'cs/cstrike/dlls/mp.dll')
    bsp = safe_file(CS / 'cstrike/maps/cs_assault.bsp')
    add(bsp, 'cs/cstrike/maps/cs_assault.bsp')
    generated('cs/cstrike/mapcycle.txt', 'cs_assault\n')
    generated('cs/cstrike/autoexec.cfg', '// Server configuration is loaded by server.cfg.\n')
    generated('cs/valve/valve.rc', 'stuffcmds\n')
    addons = CS / 'cstrike/addons'
    add(addons / 'metamod/metamod.dll', 'cs/cstrike/addons/metamod/metamod.dll')
    add(addons / 'metamod/plugins.ini', 'cs/cstrike/addons/metamod/plugins.ini')
    for name in ('sypb.dll', 'sypb.cfg'):
        add(addons / 'sypb' / name, 'cs/cstrike/addons/sypb/' + name)
    add(addons / 'sypb/LICENSE', 'licenses/SyPB.txt')
    for project in ('ReHLDS', 'ReGameDLL_CS'):
        for pattern in ('LICENSE*', 'COPYING*'):
            for source in (ROOT / 'external' / project).glob(pattern):
                if source.is_file():
                    add(source, f'licenses/{project}-{source.name}')
    tree(addons / 'sypb/language', 'cs/cstrike/addons/sypb/language')
    add(addons / 'sypb/wptdefault/cs_assault.pwf', 'cs/cstrike/addons/sypb/wptdefault/cs_assault.pwf')
    generated('cs/cstrike/addons/sypb/wptdefault/data/.keep', '')
    generated('cs/cstrike/addons/sypb/logs/.keep', '')
    amxx = addons / 'amxmodx'
    for directory in ('dlls', 'modules'):
        tree(amxx / directory, f'cs/cstrike/addons/amxmodx/{directory}', {'.dll'})
    tree(amxx / 'data/lang', 'cs/cstrike/addons/amxmodx/data/lang', {'.txt'})
    tree(amxx / 'data/gamedata', 'cs/cstrike/addons/amxmodx/data/gamedata', {'.txt'})
    # Keep only active plugins, including the registered ZP plugin list.
    for name in ('plugins.ini', 'plugins-zp50_ammopacks.ini'):
        source = amxx / 'configs' / name
        lines = source.read_text(encoding='utf-8-sig').splitlines()
        active = []
        for line in lines:
            value = line.split(';', 1)[0].strip()
            if value:
                plugin = value.split()[0]
                if not re.fullmatch(r'[\w-]+\.amxx', plugin) or 'test' in plugin or 'fixture' in plugin:
                    raise ValueError(f'Unexpected active plugin: {plugin}')
                add(amxx / 'plugins' / plugin, 'cs/cstrike/addons/amxmodx/plugins/' + plugin)
                active.append(plugin)
        generated('cs/cstrike/addons/amxmodx/configs/' + name, '\n'.join(active) + '\n')
    config_names = ('amxx.cfg', 'core.ini', 'modules.ini', 'hamdata.ini', 'cmdaccess.ini',
                    'clcmds.ini', 'cmds.ini', 'configs.ini', 'custommenuitems.cfg', 'cvars.ini',
                    'maps.ini', 'miscstats.ini', 'speech.ini', 'stats.ini', 'nade_modes.cfg',
                    'zombieplague.cfg', 'zombieplague.ini', 'zp_extraitems.ini',
                    'zp_humanclasses.ini', 'zp_zombieclasses.ini')
    for name in config_names:
        add(amxx / 'configs' / name, 'cs/cstrike/addons/amxmodx/configs/' + name)
    generated('cs/cstrike/addons/amxmodx/configs/users.ini',
              '; Add your own Steam ID admin here. No development account is included.\n')
    generated('cs/cstrike/addons/amxmodx/configs/sql.cfg', '// SQL administration is not configured.\n')
    generated('cs/cstrike/server.cfg', '''hostname "GoldCraft"
sv_lan 0
sv_password ""
mp_freezetime 0
mp_roundtime 9
mp_timelimit 0
mp_autoteambalance 0
mp_limitteams 0
mp_autokick 0
developer 0
log off
exec addons/sypb/sypb.cfg
mc_map_mining_persist 0
''')
    runtime = ROOT / '.tools/neoforge-runtime' / PIN['neoforge']
    args_path = runtime / f"libraries/net/neoforged/neoforge/{PIN['neoforge']}/win_args.txt"
    args_text = args_path.read_text(encoding='utf-8')
    libs = set(re.findall(r'libraries/[^;\s]+\.jar', args_text))
    # FML 4.0.45 ProductionServerProvider locates SRG + resources and
    # NeoForge's patched classes implicitly from its version arguments.
    libs.update((f"libraries/net/neoforged/neoforge/{PIN['neoforge']}/neoforge-{PIN['neoforge']}-server.jar",
                 f"libraries/net/neoforged/neoforge/{PIN['neoforge']}/neoforge-{PIN['neoforge']}-universal.jar",
                 f"libraries/net/minecraft/server/{PIN['version']}-{PIN['neoform']}/server-{PIN['version']}-{PIN['neoform']}-extra.jar",
                 f"libraries/net/minecraft/server/{PIN['version']}-{PIN['neoform']}/server-{PIN['version']}-{PIN['neoform']}-srg.jar"))
    for name in sorted(libs):
        add(runtime / name, 'minecraft/' + name)
    add(args_path, 'minecraft/launch.args')
    tree(MC / 'mods', 'minecraft/mods', {'.jar'})
    core = safe_file(core_jar)
    with zipfile.ZipFile(core) as jar:
        if 'dev/goldcraft/net/CsAdmission.class' not in jar.namelist():
            raise ValueError('GoldCraft JAR lacks automatic CS admission; refuse an open backend release')
    core_name = 'goldcraft-neoforge-0.1.0-dev.jar'
    files['minecraft/mods/' + core_name] = core
    add(core, 'client-update/' + core_name)
    # Only root Mod options; generated model trees and external AI service
    # credentials below config/ are not release inputs. Mods regenerate defaults.
    for directory in ('config', 'defaultconfigs'):
        for source in sorted((MC / directory).glob('*')):
            if source.is_file() and source.suffix in {'.toml', '.properties'}:
                add(source, f'minecraft/{directory}/{source.name}')
    # Keep server datapacks only. No old level.dat, playerdata or map dimensions.
    settings = {'biome': 'minecraft:plains', 'features': False, 'lakes': False,
                'layers': [{'height': 1, 'block': 'minecraft:air'}], 'structure_overrides': []}
    pack = 'minecraft/goldcraft-worlds/datapacks/goldcraft_host_maps/'
    generated(pack + 'pack.mcmeta', json.dumps({'pack': {'pack_format': 48, 'description': 'GoldCraft host maps'}}))
    dimension = f'cs_assault_{zlib.crc32(bsp.read_bytes()):08x}'
    generated(pack + f'data/goldcraft/dimension/{dimension}.json',
              json.dumps({'type': 'minecraft:overworld', 'generator': {'type': 'minecraft:flat', 'settings': settings}}))
    props = {'server-ip': '', 'server-port': '25565', 'max-players': '32',
             'online-mode': 'true', 'white-list': 'false', 'enforce-whitelist': 'false',
             'enforce-secure-profile': 'true', 'enable-rcon': 'false', 'enable-query': 'false',
             'enable-status': 'true', 'hide-online-players': 'true', 'level-name': 'goldcraft-worlds',
             'level-type': 'minecraft:flat', 'generator-settings': json.dumps(settings, separators=(',', ':')),
             'gamemode': 'creative', 'force-gamemode': 'false', 'difficulty': 'normal',
             'generate-structures': 'false', 'allow-flight': 'true', 'spawn-protection': '0',
             'view-distance': '5', 'simulation-distance': '5', 'motd': 'GoldCraft Minecraft backend',
             'broadcast-rcon-to-ops': 'false', 'op-permission-level': '4', 'log-ips': 'false'}
    generated('minecraft/server.properties', ''.join(f'{k}={v}\n' for k, v in props.items()))
    generated('minecraft/eula.txt', '# https://aka.ms/MinecraftEULA\neula=false\n')
    for name in ('whitelist.json', 'ops.json', 'banned-ips.json', 'banned-players.json'):
        generated('minecraft/' + name, '[]\n')
    jdk = ROOT / '.tools/java/jdk-21.0.12.1+1'
    # Runtime image only: leave the 100+ MiB compiler jmods/include behind.
    for directory in ('bin', 'conf', 'lib', 'legal'):
        tree(jdk / directory, 'runtime/java/' + directory)
    for name in ('runtime/java/lib/src.zip', 'runtime/java/lib/ct.sym'):
        files.pop(name, None)
    for name in ('release', 'NOTICE'):
        add(jdk / name, 'runtime/java/' + name)
    for path in sorted(TEMPLATES.rglob('*')):
        if path.is_file():
            add(path, path.relative_to(TEMPLATES).as_posix())
    add(ROOT / 'docs/SERVER_PACKAGE.md', 'README.md')
    add(ROOT / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')
    for name in ('AMXModX', 'ReAPI', 'ReGameDLL_CS', 'ReHLDS', 'YsmGeoCompat'):
        add(ROOT / f'notices/{name}-LICENSE.txt', f'licenses/{name}-LICENSE.txt')
    # Client information is intentionally a manifest, never developer profiles.
    generated('client-requirements.json', json.dumps({'minecraft': PIN['version'],
              'neoforge': PIN['neoforge'], 'wireProtocol': 30, 'nativeMessageCount': 77,
              'mods': [n.removeprefix('minecraft/mods/') for n in files if n.startswith('minecraft/mods/')]}, indent=2))
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--steam-runtime', type=Path, required=True,
                        help='Read-only directory containing Steam server API redistributables')
    parser.add_argument('--native-crt', type=Path, required=True,
                        help='Read-only x86 Microsoft Visual C++ CRT redistributable directory')
    parser.add_argument('--core-jar', type=Path, default=MC / 'mods/goldcraft-neoforge-0.1.0-dev.jar',
                        help='Verified matching GoldCraft JAR with configuration-stage admission')
    parser.add_argument('--output', type=Path, default=ROOT / 'server-packages')
    parser.add_argument('--core-source-revision', default=None,
                        help='Git source revision used for the separately verified core JAR')
    parser.add_argument('--core-source-patch', type=Path,
                        help='Optional source patch applied to that revision; only its hash is recorded')
    parser.add_argument('--plan', action='store_true')
    args = parser.parse_args()
    files = build_plan(args.steam_runtime.resolve(), args.native_crt.resolve(), args.core_jar.resolve())
    total = sum(len(v) if isinstance(v, bytes) else v.stat().st_size for v in files.values())
    print(json.dumps({'files': len(files), 'uncompressedMiB': round(total / 1048576, 1)}), flush=True)
    if args.plan:
        return
    output = args.output.resolve()
    if not output.is_relative_to(ROOT) or output == ROOT:
        raise ValueError('Package output must be a new workspace subdirectory')
    for parent in (output, *output.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError('Reparse output rejected')
    output.mkdir(parents=True, exist_ok=True)
    archive = output / 'GoldCraft-Servers-Windows-MC1.21.1.zip'
    if archive.exists():
        raise FileExistsError('Archive exists; choose a new output directory for a new release')
    # Reject copied local credentials without ever printing their values.
    import importlib.util
    spec = importlib.util.spec_from_file_location('public_scan', ROOT / 'tools/Check-PublicSource.py')
    scan = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(scan)
    private = [v.encode() for v in scan.local_private_values() if len(v) >= 12]
    private += [m.group(1) for m in re.finditer(rb'(?:rcon_password\s+"|rcon.password=)([^"\r\n]+)',
                (CS / 'cstrike/server.cfg').read_bytes() + b'\n' + (MC / 'server.properties').read_bytes())]
    manifest = {'format': 1, 'sourceRevision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'serverBaseline': '728aff08', 'minecraft': PIN['version'], 'neoforge': PIN['neoforge'],
                'nativeMessageCount': 77, 'maxPlayers': 32,
                'admission': 'online-account-and-live-cs-binding', 'files': []}
    manifest['coreSourceRevision'] = args.core_source_revision or manifest['sourceRevision']
    if args.core_source_patch:
        manifest['coreSourcePatchSha256'] = hashlib.sha256(safe_file(args.core_source_patch).read_bytes()).hexdigest()
    temporary = archive.with_suffix('.zip.partial')
    with zipfile.ZipFile(temporary, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as bundle:
        for name, source in sorted(files.items()):
            data = source if isinstance(source, bytes) else source.read_bytes()
            if Path(name).suffix.lower() in {'.json', '.cfg', '.ini', '.txt', '.properties', '.toml', '.ps1', '.cmd', '.md', '.args'}:
                if any(v in data for v in private):
                    raise ValueError(f'Private data found in package member: {name}')
                if re.search(rb'[A-Za-z]:[\\/](?:Users|MineCraft_Cs)[\\/]', data):
                    raise ValueError(f'Nonportable package member: {name}')
            bundle.writestr('GoldCraft-Servers/' + name, data)
            manifest['files'].append({'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        manifest_bytes = (json.dumps(manifest, indent=2) + '\n').encode()
        bundle.writestr('GoldCraft-Servers/package-manifest.json', manifest_bytes)
    os.replace(temporary, archive)
    (output / 'package-manifest.json').write_bytes(manifest_bytes)
    with archive.open('rb') as stream:
        checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
    (output / 'SHA256SUMS.txt').write_text(f'{checksum}  {archive.name}\n', encoding='ascii')
    print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size, 'sha256': checksum}), flush=True)


if __name__ == '__main__':
    main()
