"""Isolated ReHLDS/ZP armor, respawn, knockback and hit effect checks. No primary server restart."""
from pathlib import Path
import importlib.util,json,sys,os,subprocess
R=Path(__file__).resolve().parents[1]
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);parser.add_argument('--migration-only',action='store_true');parser.add_argument('--gore-only',action='store_true');args=parser.parse_args()
O=args.output.resolve();assert O.is_relative_to((R/'build').resolve());O.mkdir(parents=True,exist_ok=False)
s=importlib.util.spec_from_file_location('zp_fixture',R/'tools/Exercise-ZombieReAPI.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class ArmorRun(m.Run):
    def prepare(self):
        super().prepare()
        self.copy(R/'build/amxx/plugins/zp50_zombie_armor.amxx',m.AMXX/'plugins/zp50_zombie_armor.amxx')
        self.copy(R/'build/amxx/plugins/zp50_zombie_respawn.amxx',m.AMXX/'plugins/zp50_zombie_respawn.amxx')
        self.copy(R/'build/amxx/plugins/zp50_zombie_knockback.amxx',m.AMXX/'plugins/zp50_zombie_knockback.amxx')
        self.copy(R/'amxx/zombie_plague/integration/lang/zp50_zombie_respawn.txt',m.AMXX/'data/lang/zp50_zombie_respawn.txt')
        self.copy(R/'build/amxx/plugins/zp50_zombie_gore.amxx',m.AMXX/'plugins/zp50_zombie_gore.amxx')
        p=m.AMXX/'configs/plugins.ini'
        self.put(p,p.read_bytes()+b'zp50_zombie_armor.amxx debug\nzp50_zombie_respawn.amxx debug\nzp50_zombie_knockback.amxx debug\nzp50_zombie_gore.amxx debug\n')
        if args.migration_only:
            old=[]
            for suffix in ('armor','respawn','knockback','gore'):
                name='goldcraft_zombie_'+suffix+'.amxx'
                self.copy(R/'build/amxx/plugins'/('zp50_zombie_'+suffix+'.amxx'),m.AMXX/'plugins'/name)
                self.copy(R/'amxx/zombie_plague/integration/configs'/('plugins-goldcraft-zombie-'+suffix+'.ini'),m.AMXX/'configs'/('plugins-goldcraft-zombie-'+suffix+'.ini'))
                old.extend([name,name])
            self.put(m.AMXX/'configs/plugins-migration-owner.ini',('\n'.join(old)+'\n').encode())
        self.put(m.GAME/'qconsole.log',b'')
        self.put(m.GAME/'cstrike/qconsole.log',b'')
    def run(self):
        self.prepare()
        env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        start=subprocess.STARTUPINFO();start.dwFlags|=subprocess.STARTF_USESHOWWINDOW;start.wShowWindow=subprocess.SW_HIDE
        self.process=subprocess.Popen([str(m.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(self.config['csPort']),'-maxplayers','4','+map','cs_assault','+exec','goldcraft_zp_reapi.cfg'],cwd=m.GAME,env=env,startupinfo=start,creationflags=subprocess.CREATE_NEW_CONSOLE)
        m.FIXTURE_LOG=m.GAME/'qconsole.log'
        self.report['pid']=self.process.pid
        self.ready()

run=ArmorRun(startup_only=True)
try:
    run.run()
    run.report['passed']=False
    if args.migration_only:
        for suffix in ('armor','respawn','knockback','gore'):
            listing=run.command('amxx plugins zp50_zombie_'+suffix)
            assert '1 plugins, 1 running' in listing, listing
        listing=run.command('amxx plugins goldcraft_zombie_')
        assert '0 plugins, 0 running' in listing, listing
        run.report.update(passed=True,checks=['retired names blocked before load; four new plugins running once despite duplicate old owner entries'])
        print('PASS: four zp50 plugins loaded once; all retired names blocked')
        sys.exit(0)
    plugins=run.command('amxx plugins zp50_zombie_armor')
    if 'bad load' in plugins or 'error' in plugins.lower():raise RuntimeError(plugins)
    run.command('exec goldcraft_zp_reapi.cfg');run.wait(.5)
    run.command('gc_zp_reapi_create')
    run.command('sv_restart 1');run.wait(2)
    if not args.gore_only:
        run.command('gc_zp_reapi_armor')
        run.command('gc_zp_reapi_respawn_begin');run.wait(1)
        run.command('gc_zp_reapi_respawn_early');run.wait(2.3)
        run.command('gc_zp_reapi_respawn_done')
        run.command('gc_zp_reapi_head_knockback')
    run.command('gc_zp_reapi_gore')
    for mode,cvar in enumerate(() if args.gore_only else ('infection','nemesis','survivor','swarm','multi','plague','armageddon')):
        run.command('sv_restart 1');run.wait(2)
        run.command('zp_'+cvar+'_allow_respawn 0')
        run.command('gc_zp_reapi_respawn_policy '+str(mode));run.wait(3.3)
        run.command('gc_zp_reapi_respawn_denied')
    # Permission can change while the timer is pending; check again at expiry.
    if not args.gore_only:
        run.command('sv_restart 1');run.wait(2)
        run.command('zp_infection_allow_respawn 1')
        run.command('gc_zp_reapi_respawn_policy 0');run.wait(.7)
        run.command('zp_infection_allow_respawn 0');run.wait(2.6)
        run.command('gc_zp_reapi_respawn_denied')
    checks=[json.loads(line) for line in (m.AMXX/'logs/goldcraft-zp-reapi.jsonl').read_text().splitlines() if line]
    errors={}
    for p in (m.AMXX/'logs').glob('error_*.log'):
        extra=p.read_bytes()[run.error_offsets.get(p,0):].decode(errors='replace')
        if extra.strip():errors[p.name]=extra
    run.report.update(checks=checks,runtimeErrors=errors,goreOnly=args.gore_only,passed=len(checks)>=(18 if args.gore_only else 73) and all(c['passed'] for c in checks) and not errors)
    print(json.dumps(dict(checks=len(checks),failed=[c for c in checks if not c['passed']],runtimeErrors=errors)))
except Exception as e:
    run.report.update(passed=False,error=str(e));raise
finally:
    run.close(O/'zombie-gore-runtime.json')
sys.exit(0 if run.report['passed'] else 1)
