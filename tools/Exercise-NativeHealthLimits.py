"""Check actual ZP class/mode health caps and ReGameDLL healing in an isolated server.

The MC maximum-health transport is outside this test. --baseline loads the seven
installed pre-fix class/mode plugins; no main-server or graphical files change.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('zp_health_base', ROOT / 'tools/Exercise-ZombieReAPI.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
NAMES = ['zp50_class_human', 'zp50_class_zombie', 'zp50_class_nemesis', 'zp50_class_survivor',
         'zp50_gamemode_infection', 'zp50_gamemode_armageddon', 'zp50_gamemode_plague']


class Run(base.Run):
    def __init__(self, output, baseline):
        super().__init__()
        self.output, self.old = output, baseline
        self.report.update(scope=__doc__, baseline=baseline)
        # Windows can reserve the old fixture port after a network/VM change.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0))
            self.config['csPort'] = sock.getsockname()[1]

    def prepare(self):
        super().prepare()
        self.put(base.TEST / 'cluster.json', (json.dumps(self.config)+'\n').encode())
        for path in (base.AMXX / 'configs').glob('plugins-*.ini'):
            self.put(path, b'; Disabled only for this isolated health regression.\n')
        lines = (base.AMXX / 'configs/plugins.ini').read_text().splitlines()
        lines = [s for s in lines if not s.startswith('goldcraft_zp_reapi_test.')]
        lines.append('goldcraft_health_limit_test.amxx debug')
        self.put(base.AMXX / 'configs/plugins.ini', ('\n'.join(lines)+'\n').encode())
        self.copy(ROOT / 'build/amxx/plugins/goldcraft_health_limit_test.amxx',
                  base.AMXX / 'plugins/goldcraft_health_limit_test.amxx')
        if self.old:
            for name in NAMES:
                self.copy(base.LIVE / 'cstrike/addons/amxmodx/plugins' / (name+'.amxx'),
                          base.AMXX / 'plugins' / (name+'.amxx'))
        self.put(base.GAME / 'cstrike/server.cfg',
                 (base.GAME / 'cstrike/server.cfg').read_bytes() + b'exec goldcraft_zp_reapi.cfg\n'
                 b'mc_health_limit_test 1\nzp_gamemode_delay 100000\nzp_prevent_consecutive_modes 0\n'
                 b'zp_spawn_protection_time 0\nzp_deathmatch 0\n')
        self.put(base.AMXX / 'logs/goldcraft-health-limit.jsonl', b'')
        for path in [base.GAME / 'qconsole.log', base.GAME / 'cstrike/qconsole.log']:
            self.put(path, b'')

    def run(self):
        self.prepare()
        env = {k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        env['GOLDCRAFT_HTTP_ENABLED'] = '0'
        base.FIXTURE_LOG.write_text('', encoding='utf-8')
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        try:
            self.process = subprocess.Popen([str(base.GAME / 'hlds.exe'), '-game', 'cstrike', '-insecure',
                '-nomaster', '-console', '-condebug', '+sv_rcon_condebug', '0', '+ip', '127.0.0.1', '-port', str(self.config['csPort']),
                '-maxplayers', '4', '+map', 'cs_assault'], cwd=base.GAME, env=env,
                startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE)
            self.report['pid'] = self.process.pid
            print(json.dumps({'ownedFixturePid': self.process.pid}), flush=True)
            self.ready()
            self.report['plugins'] = self.command('amxx plugins')
            self.report['modules'] = self.command('amxx modules')
            if 'bad load' in self.report['plugins'] or not any('GoldCraft class health' in s and s.rstrip().endswith('debug')
                                                               for s in self.report['plugins'].splitlines()):
                raise RuntimeError('Health fixture plugin did not load')
            self.command('gc_health_create')
            self.command('sv_restart 1'); self.wait(1.7)
            self.command('gc_health_classes')
            for variant in range(3):
                self.command('sv_restart 1'); self.wait(1.7)
                self.command('gc_health_mode '+str(variant))
            checks = [json.loads(s) for s in (base.AMXX / 'logs/goldcraft-health-limit.jsonl').read_text().splitlines() if s]
            failed = [c['name'] for c in checks if not c['passed']]
            errors = {}
            for path in (base.AMXX / 'logs').glob('error_*.log'):
                extra = path.read_bytes()[self.error_offsets.get(path, 0):].decode('utf-8', errors='replace')
                if extra.strip(): errors[path.name] = extra
            self.report.update(checks=checks, failed=failed, runtimeErrors=errors,
                               passed=len(checks)==173 and not failed and not errors)
            if self.old:
                self.report['expectedRegressionReproduced'] = not errors and all(
                    name in failed for name in ['zombie/class_maximum', 'zombie/native_healing',
                                                'nemesis/class_maximum', 'nemesis/native_healing'])
        finally:
            logs = [p.read_text('utf-8', errors='replace') for p in [base.GAME/'qconsole.log', base.GAME/'cstrike/qconsole.log'] if p.exists()]
            base.FIXTURE_LOG.write_text('\n'.join(logs).replace(self.config['csRconToken'], '[redacted]'), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--name', default='run-'+str(time.time_ns()))
    args = parser.parse_args()
    if Path(args.name).name != args.name: parser.error('Use a single output directory name')
    output = ROOT / 'build/native-health-limits-20261017' / args.name
    if any(p.is_symlink() or p.is_junction() for p in [output, *output.parents]):
        raise ValueError('Linked output directory refused')
    output.mkdir(parents=True, exist_ok=False)
    base.FIXTURE_LOG = output / 'server.log'
    run = Run(output, args.baseline)
    try:
        run.run()
    except Exception as exc:
        run.report.update(error=str(exc), passed=False)
        raise
    finally:
        run.close(output / 'result.json')
    success = run.report.get('expectedRegressionReproduced', False) if args.baseline else run.report['passed']
    raise SystemExit(0 if success and run.report['fixtureFilesRestored'] and run.report['fixtureStopped'] else 1)
