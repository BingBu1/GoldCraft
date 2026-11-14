#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cold-start the complete guest pistol closure in the unused isolated server.

No graphical client, fake player, gameplay injection or main-server changes.
Every installed file is restored, including pre-existing hard-linked resources.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.parse
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import csol_hud_resources
from win32_processes import processes


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked(path):
    path = Path(path).absolute()
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError('Workspace-only test path')
    if any(p.exists() and p.lstat().st_file_attributes & 0x400 for p in (path, *path.parents)):
        raise ValueError('Reparse test path')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('mp', 'pdb', 'client', 'engine', 'resources', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--debugger', type=Path, help='Optional existing x86 CDB for cold-start crash capture')
    args = parser.parse_args()
    out = checked(args.output)
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh build receipt directory')
    game = checked(ROOT / 'sandbox/cs-dione-probe/Half-Life')
    for row in processes(('hlds.exe',)):
        if row.get('imageError') or row.get('image') and str(game).casefold() in row['image'].casefold():
            raise ValueError('Isolated server is occupied or inaccessible')
    for key in ('mp', 'pdb', 'client', 'engine', 'resources'):
        checked(getattr(args, key)).resolve(strict=True)
    inputs = {str(p): sha(p) for p in (Path(__file__), args.mp, args.pdb, args.client, args.engine, args.resources)}
    resources = json.loads(args.resources.read_text('utf8'))['files']
    if not resources or len(resources) > 1024:
        raise ValueError('Bounded explicit resource closure required')
    out.mkdir()
    rollback = out / 'rollback'
    rollback.mkdir()
    changes, child = [], None
    result = dict(success=False, sourceCopies=0, mainRuntimeTouched=False, gameplay=False,
                  checks={}, http=[], registration=[])
    cstrike = game / 'cstrike'
    console = game / 'qconsole.log'
    parked = rollback / 'qconsole-before.log'
    moved = console_owned = False
    rcon = module('guest_cold_rcon', ROOT / 'tools/GoldSrc-Command.py')
    pair = module('guest_cold_pair', ROOT / 'tools/Verify-CSOLPresentationPair.py')
    token = secrets.token_hex(24)

    def replace(target, source):
        target, source = checked(target), checked(source)
        if not target.resolve().is_relative_to(game):
            raise ValueError('Test target outside isolated server')
        before = sha(target) if target.is_file() else None
        if before == sha(source):
            return
        backup = rollback / str(len(changes))
        if before:
            shutil.copy2(target, backup)
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + '.cof-cold')
        if temporary.exists():
            raise ValueError('Previous test temporary requires inspection')
        shutil.copy2(source, temporary)
        assert temporary.stat().st_nlink == 1 and sha(temporary) == sha(source)
        os.replace(temporary, target)
        changes.append(dict(target=str(target), before=before, backup=str(backup) if before else None))

    def free_port(kind):
        with socket.socket(socket.AF_INET, kind) as connection:
            connection.bind(('127.0.0.1', 0))
            return connection.getsockname()[1]

    cs_port, http_port = free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
    private = ROOT / 'sandbox/cs-dione-probe/cof-cold-private.json'
    private.write_text(json.dumps(dict(csPort=cs_port, csRconToken=token)), 'utf8')
    (out / 'server.cfg').write_text(
        f'hostname "GoldCraft isolated CoF cold"\nrcon_password "{token}"\n'
        f'sv_lan 0\nlog off\nsv_logecho 0\nmp_timelimit 0\nsypb_quota 0\ncsol_weapons 1\n'
        f'mc_http_download 1\nmc_http_bind "127.0.0.1"\nmc_http_port {http_port}\n'
        f'mc_http_public_url "http://127.0.0.1:{http_port}/goldcraft/"\n', 'utf8')
    config = cstrike / 'addons/goldcraft/config/gameplay.json'
    settings = json.loads(config.read_text('utf-8-sig'))
    settings['features']['time_cycle']['enabled'] = False
    (out / 'gameplay.json').write_text(json.dumps(settings, ensure_ascii=False, indent=2), 'utf8')
    try:
        names = set()
        for row in resources:
            name = row['destination']
            relative = PurePosixPath(name)
            if name in names or relative.is_absolute() or '..' in relative.parts or any(c in name for c in ':\\'):
                raise ValueError('Unsafe or duplicate resource destination')
            names.add(name)
            source = checked(row['source'])
            if sha(source) != row['sha256'] or source.stat().st_size != row['bytes']:
                raise ValueError('Prepared resource identity changed')
            replace(cstrike / relative, source)
        replace(cstrike / 'dlls/mp.dll', args.mp)
        replace(game / 'swds.dll', args.engine)
        replace(config, out / 'gameplay.json')
        replace(cstrike / 'server.cfg', out / 'server.cfg')
        result['hud'] = csol_hud_resources.verify(cstrike)
        assert result['hud']['success']
        menu = pair.compiled_menu(args.client)
        paths = pair.menu_resources(menu, (cstrike / menu).read_bytes())
        result['menu'] = pair.verify_pair(args.mp, args.client, {name: cstrike / name for name in paths})
        if console.exists():
            os.replace(console, parked)
            moved = True
        console_owned = True
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        env = {k: v for k, v in os.environ.items() if not k.startswith(('GOLDCRAFT_', 'METAHOOK_'))}
        env.update(GOLDCRAFT_HTTP_ENABLED='1', GOLDCRAFT_HTTP_BIND='127.0.0.1',
                   GOLDCRAFT_HTTP_PORT=str(http_port), GOLDCRAFT_HTTP_PUBLIC_URL=f'http://127.0.0.1:{http_port}/goldcraft/')
        command = [str(game / 'hlds.exe'), '-game', 'cstrike', '-insecure', '-nomaster',
            '-console', '-condebug', '+ip', '127.0.0.1', '-port', str(cs_port), '-maxplayers', '32',
            '-csol_game_mode', '2', '+map', 'zs_deadend']
        if args.debugger:
            debugger = args.debugger.resolve(strict=True)
            if debugger.name.casefold() != 'cdb.exe':
                raise ValueError('Use the installed x86 CDB')
            dump = (out / 'cold-exception.dmp').as_posix()
            commands = out / 'debugger-commands.txt'
            commands.write_text(
                f'bu swds!Sys_Error "da poi(@esp+4); da poi(@esp+8); kp 30; .dump /m /o {dump}; q"\n'
                f'sxe -c ".exr -1; .ecxr; kp 30; .dump /m /o {dump}; q" av\ng\n', 'ascii')
            command = [str(debugger), '-G', '-logo', str(out / 'debugger.log'),
                       '-y', str(args.pdb.parent), '-cf', str(commands), *command]
        child = subprocess.Popen(command, cwd=game, env=env,
            startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
        result['pid'] = child.pid
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            if child.poll() is not None:
                result['exitCode'] = child.returncode
                raise RuntimeError('Isolated server exited before readiness')
            try:
                status = rcon.command('status', config_path=private)
                if 'zs_deadend' in status and '0 active' in status:
                    break
            except (TimeoutError, ConnectionResetError, ConnectionRefusedError):
                pass
            time.sleep(.25)
        else:
            raise TimeoutError('Cold startup did not become ready')
        result['checks'].update(slots32='32 max' in status, noPlayers='0 active' in status,
                               mode2='"2"' in rcon.command('csol_game_mode', config_path=private))
        sys.path.insert(0, str(ROOT / 'tests/csol-dione'))
        inspector = module('guest_cold_memory', ROOT / 'tests/csol-dione/inspect_runtime.py')
        targets = [row for row in processes(('hlds.exe',)) if row.get('image') and
                   Path(row['image']).resolve() == game / 'hlds.exe']
        if len(targets) != 1:
            raise ValueError('Expected one actual isolated game process')
        process = inspector.Process(targets[0]['pid'])
        try:
            loaded = next(row for row in process.modules() if Path(row['path']).name.casefold() == 'mp.dll')
            pe = inspector.reader.PE(loaded['path'])
            symbols = inspector.reader.Symbols(args.pdb, pe)
            assert pe.sha256 == sha(args.mp)
            assert process.read(loaded['base'] + pe.codeview['rva'], 24).hex() == pe.codeview['identity']
            match = re.search(r'S_LDATA32[^\n]*::extendedInfo`\s*\n[^\n]*addr = (\d+):(\d+)', symbols.globals)
            if not match:
                raise ValueError('Matching PDB lacks actual extended item table')
            table = loaded['base'] + pe.sections[int(match[1]) - 1]['rva'] + int(match[2])
            layout = symbols.layout('ItemInfo')
            for ident, name, clip in ((896, 'glock', 15), (897, 'p345', 8), (898, 'revolver', 5), (899, 'vp70', 18)):
                raw = process.read(table + (ident - 32) * layout['size'], layout['size'])
                get = lambda field: struct.unpack_from('<i', raw, layout['members'][field])[0]
                pointer = struct.unpack_from('<I', raw, layout['members']['pszName'])[0]
                actual = bytearray()
                for offset in range(64):
                    value = process.read(pointer + offset, 1)
                    if value == b'\0':
                        break
                    actual.extend(value)
                assert (get('iId'), get('iMaxClip'), actual.decode('ascii')) == (ident, clip, 'weapon_cof_' + name)
                result['registration'].append(dict(id=ident, name=actual.decode('ascii'), clip=clip))
            result['checks']['fourActualItemDefinitions'] = len(result['registration']) == 4
            result['loaded'] = dict(sha256=pe.sha256, pdb=symbols.identity)
        finally:
            process.close()

        def download(row):
            name = row['destination']
            url = f'http://127.0.0.1:{http_port}/goldcraft/' + urllib.parse.quote(name, safe='/')
            try:
                with urllib.request.urlopen(url, timeout=10) as response:
                    assert response.status == 200
                    data = response.read(row['bytes'] + 1)
            except Exception as error:
                raise RuntimeError('Guest HTTP failed for ' + name + ': ' + str(error)) from error
            assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256']
            return dict(path=name, bytes=len(data), sha256=row['sha256'])
        guest_roots = ('models/guest/cof/', 'sound/guest/cof/', 'sprites/guest/cof/',
                       'gfx/guest/cof/', 'events/guest/cof/', 'inventoryitems/guest/cof/',
                       'aurora/guest/cof/', 'sprites/weapon_cof_')
        guest = [row for row in resources if row['destination'].startswith(guest_roots)]
        with ThreadPoolExecutor(max_workers=4) as pool:
            result['http'] = list(pool.map(download, guest))
        result['checks']['actualGuestHttpHashes'] = bool(guest) and len(result['http']) == len(guest)
        private_url = f'http://127.0.0.1:{http_port}/goldcraft/configs/guest/cof/scenario_damage.json'
        try:
            with urllib.request.urlopen(private_url, timeout=10):
                raise ValueError('Server-only damage settings were exposed through HTTP')
        except urllib.error.HTTPError as error:
            if error.code != 404:
                raise
            result['checks']['serverOnlyDamageSettingsDenied'] = True
        result['checks']['sourceInputsStable'] = all(sha(Path(path)) == digest for path, digest in inputs.items())
        result['success'] = all(result['checks'].values())
    except BaseException as error:
        result['error'] = (type(error).__name__ + ': ' + str(error)).replace(token, '[redacted]')
    finally:
        if child and child.poll() is None:
            try:
                rcon.command('quit', config_path=private)
            except Exception:
                pass
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.terminate()
                child.wait(timeout=5)
        result['restored'] = True
        for row in reversed(changes):
            target = Path(row['target'])
            if row['backup']:
                temporary = target.with_name(target.name + '.cof-restore')
                shutil.copy2(row['backup'], temporary)
                os.replace(temporary, target)
                result['restored'] &= sha(target) == row['before']
            else:
                target.unlink()
        if console_owned and console.exists():
            (out / 'console.log').write_text(console.read_text('utf8', errors='replace').replace(token, '[redacted]'), 'utf8')
            console.unlink()
        if moved:
            os.replace(parked, console)
        result['success'] &= result['restored']
        result['changes'] = changes
        (out / 'receipt.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(json.dumps({key: result.get(key) for key in ('success', 'restored', 'checks', 'error')}))
    raise SystemExit(0 if result['success'] else 1)


if __name__ == '__main__':
    main()
