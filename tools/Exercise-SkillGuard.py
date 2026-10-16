"""Run real ReHLDS/ZP infection and damage paths with an explicit MC peer fixture.

Only the inactive headless sandbox is modified, and all changed files are restored.
Use a matching headless ReGameDLL build and frozen skill-guard candidate. The peer
substitutes MC state/replies; this is not paired Minecraft or graphical acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tests/skill-guard'))
from native_peer import AuthorityPeer, CAPABILITIES

spec = importlib.util.spec_from_file_location('skill_native_base', ROOT / 'tools/Exercise-ZombieReAPI.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
CANDIDATE = ROOT / 'build/candidates/combined-skill-guard-20261016'


class Run(base.Run):
    def __init__(self, output, baseline_pawn=False):
        super().__init__()
        self.output = output
        self.peer = None
        self.identity = uuid.uuid4().bytes
        self.bindings = base.TEST / 'skill-test-bindings.bin'
        self.report.update(scope=__doc__, checks={}, snapshots=[])
        self.command_at = 0
        self.rules = ROOT / 'build/candidates/combined-tnt-renderer-20261016' if baseline_pawn else CANDIDATE
        self.report['baselinePawn'] = baseline_pawn
        self.report['sources'] = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in [
            'tools/Exercise-SkillGuard.py', 'tests/skill-guard/native_peer.py',
            'amxx/zombie_plague/tests/goldcraft_skill_guard_test.sma']}

    def check(self, name, value):
        self.report['checks'][name] = bool(value)
        print(json.dumps({'check': name, 'passed': bool(value)}), flush=True)
        if not value:
            raise AssertionError(name)

    def until(self, predicate, label, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if self.process and self.process.poll() is not None:
                raise RuntimeError('Owned native server exited')
            if self.peer and self.peer.error:
                raise self.peer.error
            try:
                value = predicate()
                if value:
                    return value
            except (OSError, json.JSONDecodeError, KeyError):
                pass
            time.sleep(.03)
        raise TimeoutError(label)

    def command(self, text):
        time.sleep(max(0, self.command_at + .8 - time.monotonic()))
        self.command_at = time.monotonic()
        return super().command(text)

    def action(self, command):
        text = self.command('gc_skill_fixture ' + command)
        matches = re.findall(r'\[GC Skill\] ([^\r\n]+)', text)
        values = {}
        for line in matches:
            values.update({key: float(val) for key, val in re.findall(r'(\w+)=(-?[\d.]+)', line)})
        self.report['snapshots'].append(dict(action=command, state=values))
        return values

    def prepare(self):
        # The native fixture's only source deviation exports fake-client GCBind
        # records locally. The production candidate never contains that facility.
        proof = json.loads((ROOT / 'build/skill-guard-native-20261016/build-checkpoint.json').read_text(encoding='utf-8'))
        if not proof['passed']:
            raise RuntimeError('Native build checkpoint did not pass')
        for name, expected in (proof['inputs'] | proof['artifacts']).items():
            if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
                raise RuntimeError('Native build input or artifact changed: ' + name)
        self.report['nativeBuildCheckpointSha256'] = hashlib.sha256((ROOT / 'build/skill-guard-native-20261016/build-checkpoint.json').read_bytes()).hexdigest()
        for candidate in {CANDIDATE, self.rules}:
            for item in json.loads((candidate / 'manifest.json').read_text())['artifacts']:
                if hashlib.sha256((ROOT / item['path']).read_bytes()).hexdigest() != item['sha256']:
                    raise RuntimeError('Frozen candidate changed')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', self.config['serverPort']))
        super().prepare()
        for path in (base.AMXX / 'configs').glob('plugins-*.ini'):
            self.put(path, b'; disabled only in the isolated skill fixture\n')
        lines = (base.AMXX / 'configs/plugins.ini').read_text().splitlines()
        lines = [s for s in lines if not s.startswith('goldcraft_zp_reapi_test.')]
        for name in ['goldcraft', 'goldcraft_skill_guard_test']:
            self.copy(ROOT / f'build/amxx/plugins/{name}.amxx', base.AMXX / f'plugins/{name}.amxx')
            lines.append(name + '.amxx debug')
        for name in ['goldcraft_zp_rules', 'zp50_gamemodes', 'zp50_human_armor']:
            self.copy(self.rules / f'server/plugins/{name}.amxx', base.AMXX / f'plugins/{name}.amxx')
            if not any(s.startswith(name + '.') for s in lines):
                lines.append(name + '.amxx debug')
        self.put(base.AMXX / 'configs/plugins.ini', ('\n'.join(lines)+'\n').encode())
        self.copy(CANDIDATE / 'server/goldcraft_amxx.dll', base.AMXX / 'modules/goldcraft_amxx.dll')
        self.put(base.AMXX / 'configs/modules.ini', b'goldcraft\nreapi\nengine\nfakemeta\nfun\nhamsandwich\ncstrike\n')
        self.copy(ROOT / 'build/regamedll-headless/Release/mp.dll', base.GAME / 'cstrike/dlls/mp.dll')
        self.put(base.GAME / 'cstrike/game_init.cfg', b'mc_map_mining 0\n')
        self.put(base.GAME / 'cstrike/server.cfg', ('sv_lan 0\nlog off\nrcon_password "'+self.config['csRconToken']+'"\n'
            'exec goldcraft_zp_reapi.cfg\nmc_default_form 1\nmc_allow_switch 1\nmc_skill_guard_test 1\n'
            'zp_gamemode_delay 100000\nzp_prevent_consecutive_modes 0\nzp_human_armor_protect 1\n').encode())
        self.put(self.bindings, b'')
        for path in [base.GAME / 'qconsole.log', base.GAME / 'cstrike/qconsole.log']:
            self.put(path, b'')

    def start(self):
        self.prepare()
        env = {key: value for key, value in os.environ.items() if not key.startswith('GOLDCRAFT_')}
        for key in ['PORT', 'SESSION', 'TOKEN']:
            env['GOLDCRAFT_SERVER_'+key] = str(self.config['server'+key.title()])
        env['GOLDCRAFT_HEADLESS_BINDINGS'] = str(self.bindings)
        env['GOLDCRAFT_SERVER_LOG'] = str(self.output / 'bridge.log')
        env['GOLDCRAFT_SERVER_STATUS'] = str(self.output / 'status.json')
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        self.process = subprocess.Popen([str(base.GAME / 'hlds.exe'), '-game', 'cstrike', '-insecure', '-nomaster', '-console', '-condebug',
            '+ip', '127.0.0.1', '-port', str(self.config['csPort']), '-maxplayers', '4', '+sv_rcon_condebug', '0', '+map', 'cs_assault'],
            cwd=base.GAME, env=env, startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report['pid'] = self.process.pid
        print(json.dumps({'ownedFixturePid': self.process.pid}), flush=True)
        def connect():
            self.peer = AuthorityPeer(self.config, self.identity)
            return self.peer
        self.until(connect, 'Native authority listening', 40)
        self.until(lambda: self.peer.world, 'World received')
        self.check('New skill capability accepted', self.peer.capabilities == CAPABILITIES)
        self.report['plugins'] = self.command('amxx plugins')
        required = ['goldcraft_skill', 'goldcraft_zp_ru', 'zp50_gamemodes.', 'zp50_human_armo']
        self.check('Required test and production rules loaded', 'bad load' not in self.report['plugins'] and all(
            any(name in line and line.rstrip().endswith(('debug', 'running')) for line in self.report['plugins'].splitlines()) for name in required))
        created = self.action('create')
        self.slot = int(created['victim'])
        self.command('sv_restart 1')
        self.wait(1.4)
        started = self.action('start')
        self.check('Real infection mode starts with spare human', started.get('started') == 1 and started['humans'] >= 2 and started['zombies'] == 1)
        self.action('reset')
        def binding():
            data = self.bindings.read_bytes()
            if len(data) < 12:
                return None
            world, count = struct.unpack_from('<QI', data)
            if world != self.peer.world or len(data) != 12+24*count:
                return None
            for n in range(count):
                slot, serial = struct.unpack_from('<II', data, 12+24*n)
                if slot == self.slot:
                    return serial, data[20+24*n:36+24*n]
        serial, token = self.until(binding, 'Native fixture GCBind token')
        self.peer.send(13, struct.pack('<QII', self.peer.world, self.slot, serial) + token + self.identity)
        self.until(lambda: self.peer.messages, 'Actual bind response')
        self.check('Actual registry accepts fixture pairing', struct.unpack_from('<I', self.peer.messages.pop(0), 16)[0] == 0)
        self.until(lambda: self.peer.actors.get(self.slot, {}).get('flags', 0) & 64, 'Paired MC form')

    def configure(self, flags, **kwargs):
        self.until(lambda: self.slot in self.peer.actors, 'Current actor')
        self.peer.configure(self.peer.actors[self.slot], flags, **kwargs)

    def reset(self, flags=1, **kwargs):
        self.peer.configure(None, None)
        self.action('reset')
        result = self.action('form 1')
        self.check('Human MC entry allowed '+str(len(self.report['checks'])), result['form'] == 1)
        self.until(lambda: self.peer.actors[self.slot]['flags'] & 64, 'MC form snapshot')
        self.configure(flags, **kwargs)
        self.wait(.15)

    def run(self):
        self.start()
        self.configure(1)
        normal = self.action('hit')
        self.check('Unprotected zombie melee infects and forces native form', normal['zombie'] == 1 and normal['form'] == 0)
        refused = self.action('form 1')
        self.check('Infected player cannot reenter MC', refused['formResult'] == -5 and refused['form'] == 0)

        self.reset(9)
        self.check('Authoritative skill flag reaches Pawn', self.action('status')['flags'] == 9)
        self.until(lambda: self.peer.actors[self.slot]['flags'] & 32, 'Actual MC movement lease')
        before = len(self.peer.requests)
        guarded = self.action('hit')
        self.check('Skill prevents immediate ZP infection', guarded['zombie'] == 0 and guarded['form'] == 1)
        self.until(lambda: len(self.peer.requests) > before, 'Real native hit queues equipment request')
        after = self.action('status')
        self.check('Skill blocks infection and zero result blocks native health loss', after['zombie'] == 0 and after['form'] == 1 and after['health'] == 500)
        self.check('Native request retains actual zombie attacker and melee kind', self.peer.requests[-1]['attacker'] == after['attacker'] and self.peer.requests[-1]['kind'] == 1)
        self.check('Direct force-infect respects the same guard', self.action('force')['zombie'] == 0)

        self.configure(5, reply=20.0)
        self.action('hit')
        armor = self.action('status')
        self.check('Armor infection guard permits reduced native damage', armor['zombie'] == 0 and armor['form'] == 1 and armor['health'] == 480)
        self.configure(1)
        revoked = self.action('hit')
        self.check('Removing guard restores normal infection and MC exit', revoked['zombie'] == 1 and revoked['form'] == 0)

        self.reset(9)
        self.check('Fresh guard present before expiry', self.action('status')['flags'] == 9)
        self.configure(9, repeat=True)
        self.wait(.25)
        expired = self.action('status')
        self.check('Duplicate revisions cannot extend skill lease', expired['flags'] == 1)
        self.check('Expired guard no longer blocks infection', self.action('hit')['zombie'] == 1)

        self.reset(9)
        dead = self.action('kill')
        self.check('Death revokes privileges while preserving connection', dead['alive'] == 0 and dead['flags'] == 1)
        self.action('respawn')
        reborn = self.action('status')
        self.check('Old life snapshots preserve only presence after respawn', reborn['flags'] == 1)

        self.reset(9)
        native = self.action('form 0')
        self.check('Native form does not inherit MC infection protection', native['form'] == 0 and self.action('hit')['zombie'] == 1)

        self.reset(1)
        forced = self.action('force')
        self.check('Unprotected direct infection also forces MC exit', forced['zombie'] == 1 and forced['form'] == 0)

        self.reset(9)
        self.report['nativeRequests'] = list(self.peer.requests)
        self.peer.close()
        self.peer = None
        disconnected = self.action('status')
        self.check('Authority disconnect revokes infection protection', int(disconnected['flags']) & 8 == 0)
        self.check('Disconnected authority cannot grant permanent immunity', self.action('hit')['zombie'] == 1)
        self.report['passed'] = True

    def close(self, output=None):
        if self.peer:
            self.report['nativeRequests'] = list(self.peer.requests)
            self.peer.close()
        self.report['runtimeErrors'] = {p.name: p.read_bytes()[getattr(self, 'error_offsets', {}).get(p, 0):].decode('utf-8', errors='replace')
            for p in (base.AMXX / 'logs').glob('error_*.log') if p.stat().st_size > getattr(self, 'error_offsets', {}).get(p, 0)}
        if self.report['runtimeErrors']:
            self.report['passed'] = False
        console = '\n'.join(p.read_text(encoding='utf-8', errors='replace') for p in [base.GAME / 'qconsole.log', base.GAME / 'cstrike/qconsole.log'] if p.exists())
        for key in ['csRconToken', 'serverSession', 'serverToken', 'rconToken']:
            console = console.replace(str(self.config[key]), '[redacted]')
        (self.output / 'runtime.log').write_text(console, encoding='utf-8')
        super().close(output=self.output / 'result.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', default='run-'+str(time.time_ns()))
    parser.add_argument('--baseline-pawn', action='store_true', help='Expect old Pawn rules to miss the new skill guard')
    args = parser.parse_args()
    if Path(args.name).name != args.name:
        raise ValueError('One output directory name required')
    output = ROOT / 'build/skill-guard-native-20261016' / args.name
    if any(p.is_symlink() or p.is_junction() for p in [output, *output.parents]):
        raise ValueError('Linked output refused')
    output.mkdir(parents=True, exist_ok=False)
    run = Run(output, args.baseline_pawn)
    try:
        run.run()
    except Exception as exc:
        run.report['passed'] = False
        run.report['error'] = str(exc)
        if args.baseline_pawn and isinstance(exc, AssertionError) and str(exc) == 'Skill prevents immediate ZP infection':
            run.report['baselineReproduced'] = True
        else:
            raise
    finally:
        run.close()
    accepted = run.report.get('baselineReproduced') and not run.report['runtimeErrors'] if args.baseline_pawn else run.report.get('passed')
    raise SystemExit(0 if accepted and run.report['fixtureFilesRestored'] and run.report['fixtureStopped'] else 1)
