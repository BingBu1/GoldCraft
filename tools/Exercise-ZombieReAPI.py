"""Bounded native ZP tests in the existing inactive headless fixture.

Restores every changed runtime file and stops only the process it starts.
No graphical client, Java runtime, primary server or protected installation write.
"""
from pathlib import Path
import hashlib
import argparse
import importlib.util
import json
import os
import re
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / 'sandbox/headless-combat'
GAME = TEST / 'Half-Life'
LIVE = ROOT / 'sandbox/cs-server/Half-Life'
AMXX = GAME / 'cstrike/addons/amxmodx'
OUT = ROOT / 'build/amxx'
FIXTURE_LOG = ROOT / 'build/logs/zp-reapi-fixture.stdout.log'
STAMP = str(time.time_ns())

spec = importlib.util.spec_from_file_location('fixture_rcon', ROOT / 'tools/GoldSrc-Command.py')
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def safe(path):
    if not path.resolve().is_relative_to(TEST.resolve()):
        raise ValueError('Fixture destination escapes its directory')
    for part in [path, *path.parents]:
        if part.is_symlink() or part.is_junction():
            raise ValueError('Reparse point in fixture destination')
        if part == ROOT:
            break
    return path

def existing_processes():
    result = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                             'Get-CimInstance Win32_Process | Where-Object {$_.Name -eq "hlds.exe"} | Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress'],
                            capture_output=True, text=True, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    rows = json.loads(result.stdout) if result.stdout.strip() else []
    return rows if isinstance(rows, list) else [rows]

class Run:
    def __init__(self, startup_only=False):
        self.startup_only = startup_only
        self.config = json.loads((TEST / 'cluster.json').read_text(encoding='utf-8-sig'))
        self.before = {}
        self.artifacts = {}
        self.process = None
        self.report = {'scope': 'Actual ReHLDS/ReGameDLL/ReAPI/ZP fake-client lifecycle and damage; no graphical input or visual acceptance.',
                       'startupOnly': startup_only,
                       'variants': [], 'commands': [], 'artifacts': self.artifacts}
        self.started = time.time()

    def put(self, path, data):
        safe(path)
        previous = path.read_bytes() if path.exists() else None
        if previous == data:
            return
        if path not in self.before:
            self.before[path] = previous
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        if path.read_bytes() != data:
            raise RuntimeError('Fixture write verification failed')

    def copy(self, source, destination):
        if not source.resolve().is_relative_to(ROOT):
            raise ValueError('Source outside workspace')
        data = source.read_bytes()
        self.put(destination, data)
        self.artifacts[source.relative_to(ROOT).as_posix()] = digest(data)

    def prepare(self):
        for process in existing_processes():
            if str(process.get('ExecutablePath', '')).lower() == str(GAME / 'hlds.exe').lower():
                raise RuntimeError('The independent headless server is already running')
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', self.config['csPort']))
        for name in ('hlds.exe', 'swds.dll', 'filesystem_stdio.dll', 'steam_api.dll', 'cstrike/dlls/mp.dll'):
            self.copy(LIVE / name, GAME / name)
        for name in ('reapi_amxx.dll', 'hamsandwich_amxx.dll', 'cstrike_amxx.dll', 'fun_amxx.dll', 'fakemeta_amxx.dll', 'engine_amxx.dll'):
            self.copy(LIVE / 'cstrike/addons/amxmodx/modules' / name, AMXX / 'modules' / name)
        names = re.findall(r'^([a-z0-9_]+)\.amxx', (ROOT / 'amxx/zombie_plague/configs/plugins-zp50_ammopacks.ini').read_text(), re.M)
        names.append('goldcraft_zp_reapi_test')
        for name in names:
            source = OUT / 'plugins'
            self.copy(source / (name + '.amxx'), AMXX / 'plugins' / (name + '.amxx'))
        # Load one selected ZP configuration; inactive alternate lists are not needed.
        for name in ('zombieplague.ini', 'zombieplague.cfg', 'zp_extraitems.ini', 'zp_humanclasses.ini', 'zp_zombieclasses.ini'):
            source = LIVE / 'cstrike/addons/amxmodx/configs' / name
            if source.exists():
                self.copy(source, AMXX / 'configs' / name)
        self.ini = (AMXX / 'configs/zombieplague.ini').read_text(encoding='utf-8-sig')
        for name in ('zombie_plague.txt', 'zombie_plague50.txt'):
            self.copy(LIVE / 'cstrike/addons/amxmodx/data/lang' / name, AMXX / 'data/lang' / name)
        for source in (ROOT / 'dist/zombieplague/media').rglob('*'):
            if source.is_file():
                self.copy(source, GAME / 'cstrike' / source.relative_to(ROOT / 'dist/zombieplague/media'))
        self.put(AMXX / 'configs/plugins.ini', ('\n'.join(name + '.amxx debug' for name in names) + '\n').encode())
        self.put(AMXX / 'configs/modules.ini', b'reapi\nengine\nfakemeta\nfun\nhamsandwich\ncstrike\n')
        self.put(GAME / 'cstrike/addons/metamod/plugins.ini', b'win32 addons/amxmodx/dlls/amxmodx_mm.dll\n')
        self.put(GAME / 'cstrike/autoexec.cfg', b'// Independent ZP ReAPI fixture.\n')
        self.put(GAME / 'cstrike/server.cfg', ('sv_lan 1\nlog off\nrcon_password "' + self.config['csRconToken'] + '"\n').encode())
        self.put(GAME / 'cstrike/goldcraft_zp_reapi.cfg', b'''mp_freezetime 0
mp_roundtime 9
mp_round_infinite 1
mp_timelimit 0
mp_autoteambalance 0
mp_limitteams 0
mp_autokick 0
mp_respawn_immunitytime 0
zp_gamemode_delay 60
zp_spawn_protection_time 0
zp_deathmatch 0
goldcraft_zp_reapi_test 1
''')
        self.error_offsets = {p: p.stat().st_size for p in (AMXX / 'logs').glob('error_*.log')}
        self.option(0)

    def option(self, value):
        ini, count = re.subn(r'(?m)^SET MODELINDEX OFFSET\s*=.*$', 'SET MODELINDEX OFFSET = ' + str(value), self.ini)
        assert count == 1
        # The legacy settings reader seeks using text-mode ftell positions on
        # Windows. Preserve its CRLF representation when changing this option.
        self.put(AMXX / 'configs/zombieplague.ini', ini.replace('\n', '\r\n').encode('utf-8'))

    def command(self, text):
        response = rcon.command(text, config_path=TEST / 'cluster.json')
        self.report['commands'].append({'command': text, 'response': response})
        if text.startswith('gc_zp_reapi_'):
            print(response.strip(), flush=True)
        return response

    def wait(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f'Fixture process exited: {self.process.returncode}')
            time.sleep(min(0.2, max(0, deadline - time.monotonic())))

    def ready(self):
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f'Fixture process exited: {self.process.returncode}')
            if re.search(r'Host_Error:|Sys_Error:', FIXTURE_LOG.read_text(errors='replace')):
                raise RuntimeError('Native fixture startup reported a fatal engine error; see build/logs/zp-reapi-fixture.stdout.log')
            try:
                response = self.command('version')
                if 'ReHLDS' in response:
                    return
            except (OSError, RuntimeError):
                pass
            time.sleep(0.3)
        raise TimeoutError('Fixture failed to become ready')

    def run(self):
        FIXTURE_LOG.parent.mkdir(parents=True, exist_ok=True)
        self.prepare()
        env = {key: value for key, value in os.environ.items() if not key.startswith('GOLDCRAFT_')}
        # No bridge listener is configured; this is an isolated native regression.
        with FIXTURE_LOG.open('w', encoding='utf-8') as log:
            self.process = subprocess.Popen([str(GAME / 'hlds.exe'), '-game', 'cstrike', '-insecure', '-nomaster', '-console',
                                             '+ip', '127.0.0.1', '-port', str(self.config['csPort']), '-maxplayers', '4',
                                             '+sv_lan', '1', '+map', 'cs_assault', '+exec', 'goldcraft_zp_reapi.cfg'],
                                            cwd=GAME, env=env, stdout=log, stderr=subprocess.STDOUT,
                                            creationflags=subprocess.CREATE_NO_WINDOW)
            self.report['pid'] = self.process.pid
            print(f'Independent native fixture PID {self.process.pid}; no B/main-server changes.', flush=True)
            self.ready()
            if self.startup_only:
                self.report['modules'] = self.command('amxx modules')
                self.report['plugins'] = self.command('amxx plugins')
                self.report['passed'] = True
                return
            for option in (0, 1):
                if option:
                    self.option(option)
                    self.command('changelevel cs_assault')
                    self.wait(2.0)
                    self.ready()
                self.command('exec goldcraft_zp_reapi.cfg')
                self.wait(0.5)
                self.command('amxx modules')
                self.command('amxx plugins')
                self.command('gc_zp_reapi_create')
                self.command('gc_zp_reapi_api ' + str(option))
                self.command('sv_restart 1')
                self.wait(2.0)
                self.command('gc_zp_reapi_combat')
                self.wait(1.4)
                self.command('gc_zp_reapi_finish')
                for _ in range(3):
                    self.command('sv_restart 1')
                    self.wait(1.6)
                    self.command('gc_zp_reapi_round')
                checks = [json.loads(line) for line in (AMXX / 'logs/goldcraft-zp-reapi.jsonl').read_text().splitlines() if line]
                failed = [entry['name'] for entry in checks if not entry['passed']]
                self.report['variants'].append({'customHitboxes': option, 'checks': checks, 'failed': failed})
                print(json.dumps({'customHitboxes': option, 'checks': len(checks), 'failed': failed}), flush=True)
            errors = {}
            for path in (AMXX / 'logs').glob('error_*.log'):
                extra = path.read_bytes()[self.error_offsets.get(path, 0):].decode('utf-8', errors='replace')
                if extra.strip():
                    errors[path.name] = extra
            self.report['runtimeErrors'] = errors
            self.report['passed'] = (len(self.report['variants']) == 2 and not errors and
                                     all(len(v['checks']) >= 50 and not v['failed'] for v in self.report['variants']))

    def close(self):
        if self.process is not None and self.process.poll() is None:
            try:
                self.command('quit')
            except (OSError, RuntimeError):
                pass
            try:
                self.process.wait(5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(5)
        for path, data in reversed(list(self.before.items())):
            safe(path)
            if data is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(data)
        self.report['fixtureFilesRestored'] = all((not p.exists()) if data is None else p.read_bytes() == data for p, data in self.before.items())
        self.report['fixtureStopped'] = self.process is None or self.process.poll() is not None
        output = ROOT / 'analysis/goldcraft-tests' / ('zp-reapi-' + STAMP + '.json')
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(self.report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'report': output.relative_to(ROOT).as_posix(), 'passed': self.report.get('passed', False),
                          'restored': self.report['fixtureFilesRestored'], 'stopped': self.report['fixtureStopped']}), flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-only', action='store_true')
    args = parser.parse_args()
    run = Run(args.startup_only)
    try:
        run.run()
    except Exception as exc:
        run.report['error'] = str(exc)
        run.report['passed'] = False
        raise
    finally:
        run.close()
    sys.exit(0 if run.report['passed'] else 1)
