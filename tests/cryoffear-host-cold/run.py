#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cold-start the complete guest firearm closure in the unused isolated server.

No graphical client or main-server changes. An optional diagnostic owns one
fake player to observe native movement and outgoing prediction snapshots.
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
import traceback
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
    parser.add_argument('--include-tmp', action='store_true',
                        help='Require all five firearm definitions; otherwise require TMP to remain absent')
    parser.add_argument('--include-mp5', action='store_true',
                        help='Require all six firearm definitions, including the independently typed MP5')
    parser.add_argument('--debugger', type=Path, help='Optional existing x86 CDB for cold-start crash capture')
    parser.add_argument('--debugger-extra', type=Path,
                        help='Optional bounded workspace CDB commands; attach after cold preparation')
    parser.add_argument('--runtime-audit', type=Path,
                        help='Optional read-only exact-version PowerShell audit of the live isolated process')
    parser.add_argument('--jump-probe', type=Path,
                        help='Optional compiled goldcraft_jump_snapshot_probe.amxx for the isolated server only')
    parser.add_argument('--hold-for-client', action='store_true',
                        help='Keep the owned server for at most 15 minutes, until output/stop is created')
    parser.add_argument('--cs-port', type=int, default=0,
                        help='Explicit localhost UDP port; zero selects a free port')
    args = parser.parse_args()
    if not 0 <= args.cs_port <= 65535:
        parser.error('--cs-port must be between 0 and 65535')
    if args.hold_for_client and args.jump_probe:
        parser.error('--hold-for-client does not create the fake jump-probe actor')
    if args.debugger_extra and (not args.debugger or not args.jump_probe):
        parser.error('--debugger-extra requires --debugger and --jump-probe')
    if args.include_mp5 and not args.include_tmp:
        parser.error('--include-mp5 requires --include-tmp for the six-firearm profile')
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
    if args.runtime_audit:
        checked(args.runtime_audit).resolve(strict=True)
        inputs[str(args.runtime_audit)] = sha(args.runtime_audit)
    if args.debugger_extra:
        checked(args.debugger_extra).resolve(strict=True)
        if args.debugger_extra.stat().st_size > 32768:
            raise ValueError('Bounded debugger commands required')
        inputs[str(args.debugger_extra)] = sha(args.debugger_extra)
    if args.jump_probe:
        checked(args.jump_probe).resolve(strict=True)
        if args.jump_probe.name != 'goldcraft_jump_snapshot_probe.amxx':
            raise ValueError('Use the bounded native jump snapshot diagnostic')
        inputs[str(args.jump_probe)] = sha(args.jump_probe)
    resources = json.loads(args.resources.read_text('utf8'))['files']
    if not resources or len(resources) > 1024:
        raise ValueError('Bounded explicit resource closure required')
    damage = [row for row in resources if row['destination'] == 'configs/guest/cof/scenario_damage.json']
    if len(damage) != 1:
        raise ValueError('Expected one explicit damage profile in the cold resource set')
    profile = json.loads(checked(damage[0]['source']).read_text('utf-8-sig'))
    expected_version = 3 if args.include_mp5 else 2 if args.include_tmp else 1
    if profile.get('version') != expected_version:
        raise ValueError('Cold-test expectations do not match the supplied damage profile version')
    out.mkdir()
    rollback = out / 'rollback'
    rollback.mkdir()
    changes, child, debugger_child, debugger_command = [], None, None, None
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

    cs_port, http_port = args.cs_port or free_port(socket.SOCK_DGRAM), free_port(socket.SOCK_STREAM)
    if args.cs_port:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
            connection.bind(('127.0.0.1', cs_port))
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
        if args.jump_probe:
            plugins = cstrike / 'addons/amxmodx/configs/plugins.ini'
            text = plugins.read_text('utf-8-sig')
            (out / 'plugins.ini').write_text(text + '\ngoldcraft_jump_snapshot_probe.amxx\n', 'utf8')
            replace(plugins, out / 'plugins.ini')
            replace(cstrike / 'addons/amxmodx/plugins/goldcraft_jump_snapshot_probe.amxx', args.jump_probe)
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
            '-csol_game_mode', '2', '+developer', '1', '+map', 'zs_deadend']
        if args.debugger:
            debugger = args.debugger.resolve(strict=True)
            if debugger.name.casefold() != 'cdb.exe':
                raise ValueError('Use the installed x86 CDB')
            dump = (out / 'cold-exception.dmp').as_posix()
            commands = out / 'debugger-commands.txt'
            commands.write_text(
                f'sxe -c ".exr -1; .ecxr; kp 30; .dump /m /o {dump}; q" av\n' +
                f'sxe -c ".exr -1; .ecxr; kp 30; .dump /m /o {dump}; q" 0xc0000409\n' +
                (args.debugger_extra.read_text('ascii') + '\n' if args.debugger_extra else '') + 'g\n', 'ascii')
            debugger_command = [str(debugger), '-G', '-logo', str(out / 'debugger.log'),
                                '-y', str(args.pdb.resolve(strict=True).parent), '-cf', str(commands)]
            if not args.debugger_extra:
                command = [*debugger_command, *command]
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
        if 'CSOL map rejected:' in console.read_text('utf8', errors='replace'):
            raise RuntimeError('Scenario map rejected; guest registration alone is not gameplay readiness')
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
            result['loaded'] = dict(sha256=pe.sha256, pdb=symbols.identity)
            def global_address(pattern):
                match = re.search(pattern, symbols.globals)
                if not match:
                    raise ValueError('Missing cold diagnostic global')
                return loaded['base'] + pe.sections[int(match[1]) - 1]['rva'] + int(match[2])
            cvar = global_address(r'S_LDATA32[^\n]*::csol_weapons`\s*\n[^\n]*addr = (\d+):(\d+)')
            cvar_layout = symbols.layout('cvar_s')
            rule_var = global_address(r'S_GDATA32[^\n]*`g_pGameRules`\s*\n[^\n]*addr = (\d+):(\d+)')
            rules = struct.unpack('<I', process.read(rule_var, 4))[0]
            result['precacheState'] = dict(weaponsEnabled=struct.unpack('<f', process.read(
                cvar + cvar_layout['members']['value'], 4))[0], rulesPointer=rules,
                rulesVtable=struct.unpack('<I', process.read(rules, 4))[0] if rules else None)
            result['registrationProbes'] = []
            expected = [(896, 'glock', 15), (897, 'p345', 8), (898, 'revolver', 5), (899, 'vp70', 18)]
            if args.include_tmp:
                expected.append((900, 'tmp', 30))
            if args.include_mp5:
                expected.append((901, 'mp5', 30))
            for ident, name, clip in expected:
                raw = process.read(table + (ident - 32) * layout['size'], layout['size'])
                get = lambda field: struct.unpack_from('<i', raw, layout['members'][field])[0]
                pointer = struct.unpack_from('<I', raw, layout['members']['pszName'])[0]
                result['registrationProbes'].append(dict(expectedId=ident, id=get('iId'),
                                                        clip=get('iMaxClip'), namePointer=pointer))
                if pointer < 0x10000:
                    raise ValueError('Expected actual guest registration is missing: ' + name)
                actual = bytearray()
                for offset in range(64):
                    value = process.read(pointer + offset, 1)
                    if value == b'\0':
                        break
                    actual.extend(value)
                assert (get('iId'), get('iMaxClip'), actual.decode('ascii')) == (ident, clip, 'weapon_cof_' + name)
                result['registration'].append(dict(id=ident, name=actual.decode('ascii'), clip=clip))
            result['checks']['actualItemDefinitions'] = len(result['registration']) == len(expected)
            for ident, enabled, label in ((900, args.include_tmp, 'TMP'), (901, args.include_mp5, 'MP5')):
                if enabled:
                    continue
                raw = process.read(table + (ident - 32) * layout['size'], layout['size'])
                result['checks']['optional' + label + 'Absent'] = (
                    struct.unpack_from('<i', raw, layout['members']['iId'])[0] == 0 and
                    struct.unpack_from('<I', raw, layout['members']['pszName'])[0] == 0)
        finally:
            process.close()

        if args.runtime_audit:
            audit = checked(args.runtime_audit).resolve(strict=True)
            completed = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', str(audit), '-TargetDirectory', str(game.parent), '-ProcessId', str(targets[0]['pid'])],
                capture_output=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
            (out / 'runtime-audit.log').write_bytes(completed.stdout + completed.stderr)
            if completed.returncode:
                raise RuntimeError('Read-only runtime audit failed')
            observed = json.loads(completed.stdout.decode('utf-8-sig'))
            result['runtimeAudit'] = observed
            result['checks']['runtimeAudit'] = observed['readOnly'] and len(observed['processes']) == 1 and all(
                row.get('matches') and not row.get('error') and row.get('identityStable') and
                row.get('guest', {}).get('active') and row.get('guest', {}).get('ready')
                for row in observed['processes'])
            if not result['checks']['runtimeAudit']:
                raise RuntimeError('Runtime audit differs from actual ready guest registration')

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
                       'aurora/guest/cof/', 'sprites/weapon_cof_', 'gfx/csol/buy/cof_',
                       'resource/csol/hud/v1/cof_')
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
        if args.jump_probe:
            result['gameplay'] = 'Owned fake actor, actual engine movement; no graphical/network client'
            result['probeBegin'] = rcon.command('gc_jump_probe_begin', config_path=private)
            if '[Jump snapshot] event=begin' not in result['probeBegin']:
                raise RuntimeError('Native jump snapshot diagnostic did not create its owned actor')
            deadline = time.monotonic() + 60
            probe_audited = False
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError('Isolated server exited during jump snapshot diagnostic')
                text = console.read_text('utf8', errors='replace')
                if 'CSOL map rejected:' in text or 'Run time error' in text:
                    raise RuntimeError('Native jump diagnostic encountered a rejected map or Pawn runtime error')
                # Observe normal frames first. Attaching before native player
                # admission changes the timing of the very path under study.
                if args.debugger_extra and debugger_child is None and time.monotonic() >= deadline - 50:
                    debugger_child = subprocess.Popen([*debugger_command, '-p', str(targets[0]['pid'])],
                        cwd=game, startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
                    ready = time.monotonic() + 20
                    while time.monotonic() < ready:
                        debug_text = (out / 'debugger.log').read_text('utf8', errors='replace') if (out / 'debugger.log').exists() else ''
                        if re.search(r'\d+:\d+> g\s', debug_text):
                            break
                        if debugger_child.poll() is not None:
                            raise RuntimeError('Isolated function debugger exited before attach completed')
                        time.sleep(.1)
                    else:
                        raise TimeoutError('Isolated function debugger did not resume the server')
                if args.runtime_audit and not probe_audited and (
                        '[Jump snapshot] event=jump n=6 ' in text or time.monotonic() >= deadline - 25):
                    observed = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                        '-File', str(args.runtime_audit.resolve()), '-TargetDirectory', str(game.parent),
                        '-ProcessId', str(targets[0]['pid'])], capture_output=True, timeout=20,
                        creationflags=subprocess.CREATE_NO_WINDOW)
                    (out / 'jump-runtime-audit.log').write_bytes(observed.stdout + observed.stderr)
                    probe_audited = True
                if '[Jump snapshot] event=end' in text:
                    if 'event=end reason=complete' not in text:
                        raise RuntimeError('Jump snapshot diagnostic stopped before completion')
                    break
                time.sleep(.1)
            else:
                raise TimeoutError('Native jump snapshot diagnostic did not finish')
            samples = re.findall(r'\[Jump snapshot\] event=sample [^\r\n]*', text)
            jumps = re.findall(r'\[Jump snapshot\] event=jump [^\r\n]*', text)
            result['jumpProbe'] = dict(samples=len(samples), departures=len(jumps))
            result['checks']['nativeJumpSnapshotProbeCompleted'] = len(samples) >= 50 and len(jumps) >= 3
            if not result['checks']['nativeJumpSnapshotProbeCompleted']:
                raise RuntimeError('Native jump diagnostic did not observe real native departures and snapshots')
        if args.hold_for_client:
            (out / 'ready.json').write_text(json.dumps(dict(pid=targets[0]['pid'], csPort=cs_port,
                httpPort=http_port, game=str(game), hud=result['hud'], menu=result['menu']['menu'],
                mpSha256=sha(args.mp), clientSha256=sha(args.client),
                expiresAfterSeconds=900, stopFile=str(out / 'stop'))), 'utf8')
            print('Isolated server ready for the authorized single-client test', flush=True)
            deadline = time.monotonic() + 900
            while not (out / 'stop').exists():
                if child.poll() is not None:
                    raise RuntimeError('Owned playtest server exited')
                if time.monotonic() >= deadline:
                    raise TimeoutError('Owned playtest server reached its 15-minute bound')
                time.sleep(.25)
            result['checks']['ownedClientTestReleased'] = True
        result['checks']['sourceInputsStable'] = all(sha(Path(path)) == digest for path, digest in inputs.items())
        result['success'] = all(result['checks'].values())
    except BaseException as error:
        result['error'] = (type(error).__name__ + ': ' + str(error)).replace(token, '[redacted]')
        (out / 'traceback.txt').write_text(traceback.format_exc().replace(token, '[redacted]'), 'utf8')
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
        if debugger_child and debugger_child.poll() is None:
            try:
                debugger_child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                debugger_child.terminate()
                debugger_child.wait(timeout=5)
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
