"""Test authoritative integer HUD messages on a separate loopback ReHLDS server.

Copies required native assets only; no Java/cache copies or desktop input.
--client also connects the existing B client, captures real framebuffers and
restores its main-server observer connection. All fixture processes expire.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / 'sandbox/vitals-test'
GAME = TEST / 'Half-Life'
LIVE = ROOT / 'sandbox/cs-server/Half-Life'
AMXX = GAME / 'cstrike/addons/amxmodx'
EVIDENCE = ROOT / 'analysis/vitals'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    result = importlib.util.module_from_spec(spec); spec.loader.exec_module(result)
    return result


def put(path, data):
    if not path.resolve().is_relative_to(TEST.resolve()):
        raise ValueError('Fixture destination escapes its workspace directory')
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError('Reparse fixture destination')
        if parent == ROOT:
            break
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def prepare():
    for n in ('hlds.exe', 'swds.dll', 'filesystem_stdio.dll', 'steam_api.dll', 'steam_appid.txt',
              'tier0.dll', 'vstdlib.dll', 'vgui.dll', 'vgui2.dll', 'SDL2.dll'):
        source = LIVE / n
        if source.is_file():
            put(GAME / n, source.read_bytes())
    paths = set()
    for source in (ROOT / 'external/ReGameDLL_CS/regamedll').rglob('*'):
        if source.suffix not in ('.cpp', '.h'):
            continue
        paths.update(re.findall(r'"((?:models|sprites|events)/[^"\r\n]+\.(?:mdl|spr|sc))"', source.read_text(errors='replace')))
        paths.update(re.findall(r'"((?:sprites|gfx|resource)/[^"\r\n]+\.(?:tga|txt|bmp|res))"', source.read_text(errors='replace')))
        paths.update('sound/' + p for p in re.findall(r'"([\w./-]+\.wav)"', source.read_text(errors='replace')))
    paths.add('maps/cs_assault.bsp')
    bsp = (LIVE / 'cstrike/maps/cs_assault.bsp').read_bytes()
    start, length = struct.unpack_from('<ii', bsp, 4)
    entities = bsp[start:start + length].decode('latin1')
    paths.update(re.findall(r'"((?:models|sprites)/[^"\r\n]+\.(?:mdl|spr))"', entities))
    for game in ('valve', 'cstrike'):
        origin = LIVE / game
        for p in origin.glob('*'):
            if p.is_file() and (p.suffix == '.wad' or p.name in ('liblist.gam', 'titles.txt', 'skill.cfg', 'delta.lst')):
                put(GAME / game / p.name, p.read_bytes())
        for folder in ('models/player', 'events'):
            for p in (origin / folder).rglob('*'):
                if p.is_file():
                    put(GAME / game / p.relative_to(origin), p.read_bytes())
        for path in paths:
            if (origin / path).is_file():
                put(GAME / game / path, (origin / path).read_bytes())
    for n in ('metamod.dll', 'config.ini'):
        put(GAME / 'cstrike/addons/metamod' / n, (LIVE / 'cstrike/addons/metamod' / n).read_bytes())
    put(GAME / 'cstrike/addons/metamod/plugins.ini', b'win32 addons/amxmodx/dlls/amxmodx_mm.dll\n')
    put(GAME / 'cstrike/dlls/mp.dll', (ROOT / 'build/regamedll/Release/mp.dll').read_bytes())
    for folder in ('dlls', 'data', 'configs', 'modules'):
        source = ROOT / '.tools/amxx-1.9.0.5303/addons/amxmodx' / folder
        for p in source.rglob('*'):
            if p.is_file() and (folder != 'modules' or p.name in ('fakemeta_amxx.dll', 'hamsandwich_amxx.dll')):
                put(AMXX / folder / p.relative_to(source), p.read_bytes())
    put(AMXX / 'modules/reapi_amxx.dll', (ROOT / '.tools/reapi-5.29.0.358/addons/amxmodx/modules/reapi_amxx.dll').read_bytes())
    put(AMXX / 'plugins/goldcraft_vitals_test.amxx', (ROOT / 'build/amxx/plugins/goldcraft_vitals_test.amxx').read_bytes())
    put(AMXX / 'configs/plugins.ini', b'goldcraft_vitals_test.amxx debug\n')
    put(AMXX / 'configs/modules.ini', b'reapi\nfakemeta\nhamsandwich\n')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    config = {'csPort': port, 'csRconToken': secrets.token_hex(24)}
    put(TEST / 'cluster.json', json.dumps(config).encode())
    put(GAME / 'cstrike/valve.rc', b'ip 127.0.0.1\nstuffcmds\n')
    put(GAME / 'cstrike/autoexec.cfg', b'// Independent integer HUD fixture.\n')
    put(GAME / 'cstrike/server.cfg', ('sv_lan 1\nlog off\nmp_freezetime 0\nmp_round_infinite 1\nmp_timelimit 0\n'
        'mp_limitteams 0\nmp_autoteambalance 0\nmp_autokick 0\nmp_respawn_immunitytime 0\n'
        'goldcraft_vitals_test 1\nrcon_password "' + config['csRconToken'] + '"\n').encode())
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', action='store_true')
    args = parser.parse_args()
    # ReHLDS reads native console input events even with stdout redirected.
    # A detached/NUL console causes repeated startup aborts. Use a console/PTY.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetStdHandle.restype = ctypes.c_void_p
    mode = ctypes.c_uint()
    kernel.GetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
    if not kernel.GetConsoleMode(kernel.GetStdHandle(-10), ctypes.byref(mode)):
        raise RuntimeError('Run this fixture from a console/PTY; ReHLDS requires console input')
    remote = module('vitals_rcon', 'GoldSrc-Command.py')
    lifecycle = module('vitals_lifecycle', 'Exercise-PrecacheTransitions.py')
    renderer = module('vitals_renderer', 'Exercise-RendererPerformance.py')
    if args.client:
        lifecycle.status() # Reject an exited/stale B before starting a server.
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    stamp = time.time_ns()
    report = {'scope': __doc__, 'checks': {}, 'cases': [], 'passed': False}
    proc = None
    config = prepare()
    def command(text):
        output = remote.command(text, config_path=TEST / 'cluster.json')
        rows = [json.loads(row) for row in re.findall(r'GC_VITALS (\{[^\r\n]+\})', output)]
        return rows if rows else output

    def check(name, condition):
        report['checks'][name] = bool(condition)
        if not condition:
            raise AssertionError(name)

    def cases(userid, slot, client=False):
        command(f'gc_vitals_select #{userid}')
        for hp, armor, wide in ((255,255,1), (256,256,1), (512,512,1), (1000,1000,1),
                (32768,32768,1), (65536,65536,1), (1000000,1000000,1), (2147483647,2147483647,1),
                (65536,65536,0), (1024,0,1)):
            rows = command(f'gc_vitals_set {hp} {armor} {wide}')
            row = next(r for r in rows if r['slot'] == slot)
            report['cases'].append({'requested': [hp,armor,wide], 'server': row, 'client': client})
            expect = [hp if wide else min(hp,255), armor if wide else min(armor,32767)]
            # AMXX message_const.inc: BYTE=1, CHAR=2, SHORT=3, LONG=4.
            check(f'{client}-{hp}-{wide}-payload', row['values'][:2] == expect and row['types'][:2] == ([4,4] if wide else [1,3]))
            check(f'{client}-{hp}-{wide}-authority', row['health'] == float(hp if hp < 2147483647 else 2147483648))
            if client:
                # Allow the separate movement/clientdata delta to arrive too.
                time.sleep(.5)
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    full = lifecycle.status()
                    state = full['nativeVitals']
                    if state['health'] == expect[0] and state['armor'] == expect[1] and state['drawnHealth'] == expect[0] and state['drawnArmor'] == expect[1]:
                        break
                    time.sleep(.1)
                report['cases'][-1]['hud'] = state
                report['cases'][-1]['viewAngles'] = full['viewAngles']
                check(f'live-{hp}-{wide}-livingView', abs(full['viewAngles'][2]) < 1)
                check(f'live-{hp}-{wide}-HUD', state['health'] == expect[0] and state['armor'] == expect[1]
                    and state['drawnHealth'] == expect[0] and state['drawnArmor'] == expect[1])
                if hp in (256,1000,65536,2147483647) and wide:
                    frame = ROOT / 'sandbox/cs-client-b/logs/goldcraft-frame.bmp'
                    old = frame.stat().st_mtime_ns if frame.exists() else 0
                    renderer.command('capture')
                    deadline = time.monotonic() + 2
                    while (not frame.exists() or frame.stat().st_mtime_ns == old) and time.monotonic() < deadline:
                        time.sleep(.05)
                    time.sleep(.15)
                    check(f'live-{hp}-freshFramebuffer', frame.exists() and frame.stat().st_mtime_ns != old)
                    screenshot = EVIDENCE / f'live-{stamp}-{hp}.bmp'
                    screenshot.write_bytes(frame.read_bytes())
                    report['cases'][-1]['framebuffer'] = screenshot.relative_to(ROOT).as_posix()
        for cmd, hp in (('gc_vitals_damage 24',1000), ('gc_vitals_heal 24',1024), ('gc_vitals_damage 2048',0), ('gc_vitals_respawn',100)):
            rows = command(cmd); row = next(r for r in rows if r['slot'] == slot)
            report['cases'].append({'command':cmd,'server':row,'client':client})
            check(f'{client}-{cmd}', row['values'][0] == hp and row['types'][0] == 4 and bool(row['alive']) == bool(hp))
            if client:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    state = lifecycle.status()['nativeVitals']
                    if state['health'] == hp and (hp == 0 or state['drawnHealth'] == hp):
                        break
                    time.sleep(.1)
                report['cases'][-1]['hud'] = state
                check('live-' + cmd, state['health'] == hp and (hp == 0 or state['drawnHealth'] == hp))
        command('gc_vitals_clear')

    try:
        env = {k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        logpath = EVIDENCE / 'isolated-server.log'
        with logpath.open('w') as log:
            proc = subprocess.Popen([str(GAME/'hlds.exe'), '-game','cstrike','-insecure','-nomaster','-console',
                '+ip','127.0.0.1','-port',str(config['csPort']),'-maxplayers','8','+sv_lan','1','+map','cs_assault'],
                cwd=GAME, env=env, stdout=log, stderr=subprocess.STDOUT)
        report['pid'] = proc.pid
        deadline = time.monotonic() + 35
        ready = False
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise RuntimeError('Independent server exited; inspect isolated-server.log')
            try:
                if 'ReHLDS' in command('version'):
                    ready = True
                    break
            except (OSError, RuntimeError): pass
            time.sleep(.2)
        if not ready:
            raise TimeoutError('Independent server startup; inspect isolated-server.log')
        report['plugins'] = command('amxx plugins')
        report['mpSha256'] = hashlib.sha256((GAME/'cstrike/dlls/mp.dll').read_bytes()).hexdigest()
        created = command('gc_vitals_create')
        identities = re.findall(r'GC_VITALS_CREATED slot=(\d+) userid=(\d+)', created)
        check('twoActualFakeClients', len(identities) == 2)
        slot, userid = map(int, identities[0]); observer, _ = map(int, identities[1])
        cases(userid, slot)
        command(f'gc_vitals_select #{userid}'); command('gc_vitals_set 4096 1000 1')
        for wide in (0,1):
            rows = command(f'gc_vitals_observe {observer} {slot} {wide}')
            row = next(r for r in rows if r['slot'] == observer)
            check(f'observer{wide}-SpecHealth2', row['values'][3] == (4096 if wide else 255) and row['target'] == slot and row['types'][3] == (4 if wide else 1))
            rows = command('gc_vitals_damage 24'); row = next(r for r in rows if r['slot'] == observer)
            check(f'observer{wide}-SpecHealth', row['values'][2] == (4072 if wide else 255) and row['types'][2] == (4 if wide else 1))
            report['cases'].append({'observer':row, 'wide':wide})
            command('gc_vitals_set 4096 1000 1')
        command('gc_vitals_clear')
        if args.client:
            renderer.command('disconnect'); time.sleep(.5)
            lifecycle.connect(config['csPort'], True)
            renderer.command('team 2'); time.sleep(2)
            rows = command('gc_vitals_status')
            humans = [r for r in rows if r['slot'] not in (slot,observer)]
            check('oneBHuman', len(humans)==1)
            human = humans[0]
            check('BAutomaticallyNegotiatesLONG', human['types'][:2] == [4,4])
            if not human['alive']:
                command('sv_restart 1'); time.sleep(2)
            cases(human['userid'],human['slot'],True)
        report['passed'] = True
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
    finally:
        if args.client:
            try:
                cluster = lifecycle.read(ROOT/'sandbox/cluster.json')
                renderer.command('disconnect'); time.sleep(.5)
                lifecycle.connect(cluster['csPort'],True); renderer.command('observer')
                report['restoredMainConnection'] = True
            except Exception as error:
                report['restorationError'] = str(error); report['passed'] = False
        if proc is not None and proc.poll() is None:
            try: command('gc_vitals_clear'); command('quit'); proc.wait(timeout=5)
            except Exception: proc.terminate(); proc.wait(timeout=5)
        report['fixtureStopped'] = proc is None or proc.poll() is not None
        out = EVIDENCE / f'run-{stamp}.json'
        out.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({'passed':report['passed'],'checks':report['checks'],'error':report.get('error'),'report':out.relative_to(ROOT).as_posix()}),flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
