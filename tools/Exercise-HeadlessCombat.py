"""Real ReHLDS/NeoForge combat without desktop input or the existing A/B server pair.

The fake client uses an actual ReGameDLL CBasePlayer. ReAPI FireBullets3 exercises
the native trace/damage path; it does not claim graphical weapon-input acceptance.
Prepare with Initialize-HeadlessCombat.ps1 -PlayerFixtures for the native fall check.
"""
import importlib.util
import hashlib
import json
import math
import os
from pathlib import Path
import re
import socket
import subprocess
import time
import zlib
import Modpack

ROOT = Path(__file__).resolve().parent.parent
LOADER = os.environ.get('GOLDCRAFT_MOD_LOADER', 'neoforge')
if LOADER not in ('neoforge', 'fabric'):
    raise ValueError('Unsupported Minecraft loader')
TEST = ROOT / 'sandbox/headless-combat'
DIM = 'goldcraft:cs_assault_f6725c06'
TAG = 'goldcraft_headless15'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


native = module('headless_native', 'GoldSrc-Command.py')
minecraft = module('headless_minecraft', 'Minecraft-Command.py')


class CombatRun:
    def __init__(self):
        if not TEST.resolve().is_relative_to((ROOT/'sandbox').resolve()):
            raise ValueError('Headless runtime must stay inside the sandbox')
        self.config = json.loads((TEST/'cluster.json').read_text(encoding='utf-8-sig'))
        self.started = time.time()
        self.stamp = str(time.time_ns())
        self.processes = {}
        self.files = []
        version = Modpack.PLATFORM['minecraft'] if LOADER == 'neoforge' else '1.21'
        self.report = {'source': f'Independent real ReHLDS/ReGameDLL/AMXX/ReAPI and Minecraft {version} servers',
                       'scope': 'Unpaired native fake-client CBasePlayer; native FireBullets3 and vanilla mob AI. No desktop/client input.',
                       'checks': {}, 'commands': [], 'samples': [], 'processes': {}}
        self.report['artifacts'] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [TEST/'Half-Life/hlds.exe', TEST/'Half-Life/swds.dll', TEST/'Half-Life/cstrike/dlls/mp.dll',
                         TEST/'Half-Life/cstrike/addons/amxmodx/plugins/goldcraft_headless_test.amxx',
                         ROOT/LOADER/'build/classes/java/main/dev/goldcraft/world/NativePlayers.class',
                         ROOT/LOADER/'build/classes/java/main/dev/goldcraft/world/NativePlayerEntity.class']}
        self.report['loader'] = LOADER

    def clean(self, text):
        for key in ('csRconToken', 'rconToken', 'serverToken', 'serverSession'):
            text = text.replace(str(self.config[key]), '[redacted]')
        return text

    def check(self, label, value):
        self.report['checks'][label] = bool(value)
        print(json.dumps({'check': label, 'passed': bool(value)}), flush=True)
        if not value:
            raise AssertionError(label)

    def alive(self):
        for name, process in self.processes.items():
            if process.poll() is not None:
                raise RuntimeError(f'{name} exited with code {process.returncode}; inspect its isolated log')

    def wait(self, predicate, label, seconds=20):
        deadline = time.monotonic() + seconds
        last = None
        while time.monotonic() < deadline:
            self.alive()
            try:
                value = predicate()
                if value:
                    return value
            except (OSError, ValueError, KeyError, StopIteration) as error:
                last = error
            time.sleep(.08)
        raise TimeoutError(f'{label}; last transient observation={last!r}')

    def launch(self, name, executable, args, cwd, environment=None, clear_environment=()):
        if not Path(executable).resolve().is_relative_to(ROOT):
            raise ValueError('Executable outside workspace')
        if not Path(cwd).resolve().is_relative_to(TEST):
            raise ValueError('Working directory outside independent test')
        env = {key: value for key, value in os.environ.items() if not key.startswith('GOLDCRAFT_')}
        for key in clear_environment:
            env.pop(key, None)
        env.update(environment or {})
        for key in ('PORT', 'SESSION', 'TOKEN'):
            env['GOLDCRAFT_SERVER_'+key] = str(self.config['server'+key.title()])
        env['GOLDCRAFT_SERVER_STATUS'] = str(TEST/'logs/goldcraft-server-status.json')
        env['GOLDCRAFT_SERVER_LOG'] = str(TEST/'logs/goldcraft-server.log')
        env['GOLDCRAFT_PERFORMANCE_LOG'] = str(TEST/f'logs/performance-{name}.json')
        out = TEST/f'logs/{name}-{self.stamp}.stdout.log'
        err = TEST/f'logs/{name}-{self.stamp}.stderr.log'
        stdout, stderr = out.open('wb'), err.open('wb')
        self.files.extend((stdout, stderr))
        # CTextConsoleWin32::GetLine requires GetNumberOfConsoleInputEvents on a
        # real console input handle. Inherit the runner's hidden PTY for ReHLDS;
        # NUL/pipe stdin makes it repeatedly stop before ServerActivate.
        console = name == 'ReHLDS'
        if console and not os.isatty(0):
            raise RuntimeError('Run this test in a console/PTY so ReHLDS receives a console input handle')
        process = subprocess.Popen([str(executable), *args], cwd=cwd, env=env,
                                   stdout=stdout, stderr=stderr, stdin=None if console else subprocess.DEVNULL,
                                   creationflags=0 if console else subprocess.CREATE_NO_WINDOW)
        self.processes[name] = process
        self.report['processes'][name] = {'pid': process.pid, 'executable': str(executable),
                                        'cwd': str(cwd), 'stdout': str(out), 'stderr': str(err)}
        (TEST/'processes.json').write_text(json.dumps(self.report['processes'], indent=2), encoding='utf-8')
        print(json.dumps({'started': name, 'pid': process.pid}), flush=True)

    def cs(self, command, record=True):
        result = native.command(command, config_path=TEST/'cluster.json')
        if record:
            self.report['commands'].append({'server': 'ReHLDS', 'command': command, 'response': result})
        return result

    def mc(self, command, record=True):
        with socket.create_connection(('127.0.0.1', self.config['minecraftRconPort']), timeout=3) as connection:
            minecraft.exchange(connection, 1, 3, self.config['rconToken'])
            result = minecraft.exchange(connection, 2, 2, command)
        if record:
            self.report['commands'].append({'server': 'Minecraft', 'command': command, 'response': result})
        return result

    def state(self, minecraft_server=False):
        path = TEST/'logs'/('minecraft-server-status.json' if minecraft_server else 'goldcraft-server-status.json')
        if path.stat().st_mtime < self.started:
            raise ValueError('Stale status from an earlier run')
        return json.loads(path.read_text(encoding='utf-8-sig'))

    def actor(self):
        return next(a for a in self.state()['actors'] if a['slot'] == self.slot)

    def fixture(self):
        value = self.cs('gc_headless_status', record=False)
        return {key: (float(val) if re.fullmatch(r'-?\d+(\.\d+)?', val) else val)
                for key, val in re.findall(r'(\w+)=([^\s]+)', value)}

    def data(self, field):
        return self.mc(f'execute in {DIM} run data get entity @e[tag={TAG},limit=1] {field}', record=False)

    def health(self):
        match = re.search(r':\s*(-?[\d.]+)f\s*$', self.data('Health'))
        return float(match[1]) if match else 0.0

    def position(self):
        value = self.data('Pos')
        match = re.search(r'\[([^\[\]]+)\]\s*$', value)
        if not match:
            raise ValueError(f'Missing test-mob position: {value}')
        return [float(part.strip().rstrip('d')) for part in match[1].split(',')]

    def anger(self):
        match = re.search(r':\s*(\d+)\s*$', self.data('AngerTime'))
        return int(match[1]) if match else 0

    def capture(self, label):
        value = {'label': label, 'elapsed': time.time()-self.started, 'native': self.state(),
                 'minecraft': self.state(True), 'fixture': self.fixture()}
        self.report['samples'].append(value)
        return value

    def clear_mobs(self):
        self.mc(f'execute in {DIM} run kill @e[tag={TAG}]')
        self.wait(lambda: self.state()['minecraftObjects'] == 0, 'old mob proxies removed')

    def reset_player(self, health=100, armor=0):
        self.cs(f'gc_headless_reset 496 176 36.03125 {health} {armor}')
        self.wait(lambda: self.actor()['alive'] and abs(self.actor()['origin'][0]-496)<.1,
                  'native player placed in cs_assault')
        self.wait(lambda: self.state(True)['nativePlayers']['players'] == 1, 'native AI body available')

    def spawn(self, kind, *, no_ai=False):
        extra = ',NoAI:1b' if no_ai else ''
        self.mc(f'execute in {DIM} run summon minecraft:{kind} 10.5 64.001 -5.5 '
                f'{{Tags:["{TAG}"],PersistenceRequired:1b{extra}}}')
        return self.wait(lambda: next((e for e in self.state()['hostEntities'] if e['minecraftKey'] != 0), None),
                         'Minecraft creature has a native hit/collision proxy')

    def run(self):
        game = TEST/'Half-Life'
        if LOADER == 'neoforge':
            with Modpack.pack_lock():
                pack = Modpack.snapshot()
                Modpack.validate(pack)
                state = TEST/'modpack-deployed.json'
                plan = Modpack.deployment_plan(pack, {'headless': ('server', TEST/'minecraft')}, state)
                Modpack.apply(pack, plan, state)
            self.report['managedPackFingerprint'] = pack['fingerprint']
            for name, entry in plan['targets']['headless']['files'].items():
                self.report['artifacts'][str((TEST/'minecraft'/name).relative_to(ROOT))] = entry['sha256']
        crc = zlib.crc32((game/'cstrike/maps/cs_assault.bsp').read_bytes())
        self.check('test map matches the prepared Minecraft dimension', crc == 0xf6725c06)
        self.launch('ReHLDS', game/'hlds.exe', ['-game', 'cstrike', '-insecure', '-nomaster', '-console',
                    '+ip', '127.0.0.1', '-port', str(self.config['csPort']), '-maxplayers', '4',
                    '+sv_lan', '1', '+map', 'cs_assault', '+exec', 'server.cfg'], game)
        self.wait(lambda: 'ReHLDS' in self.cs('version', record=False), 'ReHLDS RCON ready', 50)
        self.report['serverStatus'] = self.cs('status')
        self.check('ReHLDS test server is bound to loopback',
                   f'127.0.0.1:{self.config["csPort"]}' in self.report['serverStatus'])
        self.report['modules'] = self.cs('amxx modules')
        self.check('actual ReAPI and GoldCraft modules loaded',
                   'ReAPI' in self.report['modules'] and 'GoldCraft' in self.report['modules'])
        runtime = 'neoforge-production' if LOADER == 'neoforge' else 'fabric-runtime'
        manifest = json.loads((ROOT/f'build/{runtime}/runServer.json').read_text(encoding='utf-8'))
        self.report['runtime'] = runtime
        classpath = ['-cp', ';'.join(manifest['classpath'])] if manifest['classpath'] else []
        args = ['-Xms256m', '-Xmx1536m', *[a for a in manifest['jvm'] if not a.startswith(('-Xms', '-Xmx'))],
                *classpath, manifest['main'], *manifest['args']]
        argfile = TEST/'server.args'
        argfile.write_text('\n'.join('"'+a.replace('\\', '\\\\').replace('"', '\\"')+'"' for a in args), encoding='utf-8')
        self.launch('Minecraft', ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe', ['@'+str(argfile)], TEST/'minecraft',
                    manifest.get('environment'), manifest.get('clearEnvironment', []))
        self.wait(lambda: 'players online' in self.mc('list', record=False), 'Minecraft RCON ready', 90)
        self.wait(lambda: self.state(True)['map'] == 'cs_assault', 'authenticated real server bridge', 45)
        self.wait(lambda: self.state()['map'] == 'cs_assault', 'first native authority diagnostic')
        self.report['initial'] = {'native': self.state(), 'minecraft': self.state(True)}
        self.mc('gamerule doMobSpawning false')
        self.mc('gamerule doDaylightCycle false')
        self.mc('time set midnight')
        self.mc('difficulty normal')
        self.mc(f'execute in {DIM} run forceload add 0 -16 31 15')
        self.cs('gc_headless_create')
        fixture = self.wait(lambda: (d if (d := self.fixture()).get('connected') == 1 and d.get('alive') == 1 else None),
                            'real native player lifecycle')
        self.slot = int(fixture['slot'])
        self.reset_player()
        self.check('ordinary native player is unpaired and uses native movement',
                   self.actor()['uuid'] == '0'*32 and not self.actor()['minecraftForm'] and not self.actor()['controlled'])
        self.check('Minecraft server has zero network players', '0 of a max' in self.mc('list'))

        self.cs(f'gc_test_fall #{self.actor()["userid"]} 10')
        self.wait(lambda: abs(self.actor()['health']-90)<.01, 'native CS fall damage remains enabled')
        self.check('ordinary CS fall damage remains native after the Minecraft fall fix',
                   not self.actor()['minecraftFallAuthority'] and abs(self.actor()['health']-90)<.01)
        self.reset_player()

        self.clear_mobs()
        proxy = self.spawn('wolf')
        start = self.position()
        before = self.health()
        self.cs(f'gc_headless_fire {proxy["slot"]} 5')
        self.wait(lambda: 0 < self.health() < before, 'actual CS bullet damages wolf')
        self.check('native FireBullets3 damages the actual Minecraft wolf', self.health() < before)
        self.wait(lambda: self.anger() > 0, 'vanilla wolf anger after native bullet')
        self.check('wolf gets vanilla anger without forced targeting', self.anger() > 0)
        self.wait(lambda: self.fixture().get('applied', 0) > 0, 'wolf bite reaches actual ReGameDLL health', 30)
        wolf = self.capture('wolf-retaliation')
        self.report['wolfMovement'] = {'start': start, 'after': self.position()}
        self.check('wolf autonomously walks over the real cs_assault floor', math.dist(start, self.position()) > 1.5)
        self.check('wolf bites through real ReAPI TakeDamage and decreases native health',
                   wolf['fixture']['source'] == 'goldcraft_object' and wolf['fixture']['health'] < 100 and
                   wolf['fixture']['applied'] > 0 and wolf['native']['mobDamageAccepted'] > 0)
        unarmored = wolf['fixture']['amount']

        self.reset_player(100, 100)
        self.wait(lambda: self.fixture().get('applied', 0) > 0, 'armored native player takes wolf bite')
        armor = self.capture('native-armor')
        self.check('native armor absorbs part of mob damage and is consumed',
                   armor['fixture']['health'] < 100 and armor['fixture']['armor'] < 100 and
                   (100-armor['fixture']['health'])/armor['fixture']['applied'] < unarmored)

        life = self.actor()['life']
        before_lethal = self.state()['mobDamageAccepted']
        self.reset_player(5, 0)
        self.wait(lambda: self.fixture().get('deaths', 0) == 1, 'vanilla mob causes real native Killed', 20)
        self.wait(lambda: not self.actor()['alive'] and self.state(True)['nativePlayers']['players'] == 0,
                  'native death invalidates Minecraft AI body')
        self.wait(lambda: self.state()['mobDamageAccepted'] > before_lethal,
                  'lethal native damage acknowledged as applied')
        death = self.capture('native-death')
        self.check('native death calls Killed once and removes the AI target', self.fixture()['deaths'] == 1)
        self.check('lethal TakeDamage FALSE return is not reported as rejected',
                   death['native']['mobDamageAccepted'] > before_lethal)

        self.clear_mobs()
        self.reset_player()
        self.check('native respawn advances the life generation', self.actor()['life'] > life)
        self.spawn('zombie')
        start = self.position()
        self.wait(lambda: self.fixture().get('applied', 0) > 0, 'zombie autonomously targets and damages native player', 35)
        zombie = self.capture('proactive-hostile')
        self.check('hostile mob independently chases and damages an unpaired CS player',
                   math.dist(start, self.position()) > 1.5 and zombie['fixture']['health'] < 100)

        self.clear_mobs()
        self.reset_player()
        proxy = self.spawn('wolf', no_ai=True)
        before = self.health()
        self.mc(f'execute in {DIM} run fill 13 64 -8 13 68 -3 minecraft:bedrock')
        self.wait(lambda: self.state()['minecraftObjects'] >= 31, 'MC wall exported to native bullet collision')
        self.cs(f'gc_headless_fire {proxy["slot"]} 5')
        time.sleep(.7)
        self.capture('native-bullet-occlusion')
        self.check('Minecraft wall blocks the actual native bullet before the wolf', self.health() == before)
        self.report['outcome'] = 'PASSED'

    def finish(self):
        # Shut down only these owned Popen children. Never use names/global PID lists.
        for name, command in [('Minecraft', lambda: self.mc('stop')), ('ReHLDS', lambda: self.cs('quit'))]:
            process = self.processes.get(name)
            if not process:
                continue
            if process.poll() is None:
                try:
                    command()
                except (OSError, ValueError, RuntimeError):
                    pass
                try:
                    process.wait(timeout=35)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=10)
            self.report['processes'][name]['exitCode'] = process.returncode
        for handle in self.files:
            handle.close()
        output = ROOT/f'analysis/goldcraft-tests/headless-combat15-{self.stamp}.json'
        output.write_text(self.clean(json.dumps(self.report, indent=2, ensure_ascii=False)), encoding='utf-8')
        print(json.dumps({'report': str(output), 'outcome': self.report.get('outcome', 'INCOMPLETE'),
                          'error': self.report.get('error')}), flush=True)


def main():
    run = CombatRun()
    try:
        run.run()
    except Exception as error:
        run.report['outcome'] = 'INCOMPLETE'
        run.report['error'] = run.clean(repr(error))
        for name, is_minecraft in [('native', False), ('minecraft', True)]:
            try:
                run.report['failure_'+name] = run.state(is_minecraft)
            except (OSError, ValueError):
                pass
        if hasattr(run, 'slot'):
            try:
                run.report['failure_fixture'] = run.fixture()
                run.report['failure_mob'] = run.data('Pos')
            except (OSError, ValueError, RuntimeError):
                pass
    finally:
        run.finish()
    return 0 if run.report['outcome'] == 'PASSED' else 1


if __name__ == '__main__':
    raise SystemExit(main())
