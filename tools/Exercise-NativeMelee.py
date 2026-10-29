"""Exercise real primary/secondary knife attacks with negotiated melee attributes.

The authenticated MC peer is a protocol fixture, not a Minecraft JVM. The real
native DLL, ReAPI hooks, Pawn VM and knife traces, armor and damage cancellation paths run unchanged.
Only this script's inactive fixture is modified, stopped and restored.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tests/skill-guard'))
from native_peer import AuthorityPeer
spec=importlib.util.spec_from_file_location('health_native_base',ROOT/'tools/Exercise-ZombieReAPI.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
CAP=4194304


class MeleePeer(AuthorityPeer):
    def __init__(self,config):
        self.desired=None
        super().__init__(config,bytes(16),CAP)
    def publish(self):
        if not self.desired:return
        a,add,base,total=self.desired;self.revision+=1
        self.send(98,struct.pack('<QQ4I3f',self.world,self.revision,a['slot'],a['serial'],a['spawn'],a['life'],add,base,total))
    def configure_melee(self,actor,add=0,base=0,total=1):
        with self.lock:self.desired=(dict(actor),add,base,total) if actor else None


class Run(base.Run):
    def __init__(self,output,server_dll=None):
        super().__init__();self.output=output;self.peer=None;self.command_at=0
        self.server_dll=(server_dll or ROOT/'build/regamedll/Release/mp.dll').resolve()
        if not self.server_dll.is_relative_to(ROOT) or not self.server_dll.is_file():
            raise ValueError('Server DLL must be an existing workspace artifact')
        self.report.update(scope=__doc__,checks={},snapshots=[])
        for key,kind in [('csPort',socket.SOCK_DGRAM),('serverPort',socket.SOCK_STREAM)]:
            with socket.socket(socket.AF_INET,kind) as s:s.bind(('127.0.0.1',0));self.config[key]=s.getsockname()[1]

    def until(self,predicate,label,seconds=12):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            if self.process and self.process.poll() is not None:raise RuntimeError('Owned fixture exited')
            if self.peer and self.peer.error:raise self.peer.error
            try:
                value=predicate()
                if value:return value
            except (OSError,KeyError):pass
            time.sleep(.03)
        raise TimeoutError(label)

    def check(self,label,ok):
        self.report['checks'][label]=bool(ok);print(json.dumps(dict(check=label,passed=bool(ok))),flush=True)
        if not ok:raise AssertionError(label)

    def command(self,text):
        time.sleep(max(0,self.command_at+.8-time.monotonic()));self.command_at=time.monotonic()
        return super().command(text)

    def action(self,text):
        output=self.command('gc_melee '+text)
        lines=re.findall(r'\[GC Melee\] ([^\r\n]+)',output)
        if not lines:raise ValueError('Missing Pawn action response: '+text)
        values={key:float(value) for key,value in re.findall(r'(\w+)=(-?[\d.]+)',lines[-1])}
        self.report['snapshots'].append(dict(action=text,state=values));return values

    def configure(self,add=0,base=0,total=1):
        actor=self.until(lambda:self.peer.actors.get(self.slot),'Current native actor')
        self.peer.configure_melee(actor,add,base,total);self.wait(.15)

    def prepare(self):
        super().prepare();self.put(base.TEST/'cluster.json',(json.dumps(self.config)+'\n').encode())
        for p in (base.AMXX/'configs').glob('plugins-*.ini'):self.put(p,b'; isolated health fixture\n')
        lines=(base.AMXX/'configs/plugins.ini').read_text().splitlines()
        lines=[s for s in lines if not s.startswith('goldcraft_zp_reapi_test.')]
        lines=['goldcraft.amxx debug','goldcraft_melee_test.amxx debug']
        self.put(base.AMXX/'configs/plugins.ini',('\n'.join(lines)+'\n').encode())
        self.put(base.AMXX/'configs/modules.ini',(base.AMXX/'configs/modules.ini').read_bytes()+b'goldcraft\n')
        self.copy(ROOT/'build/native-x86/Release/goldcraft_amxx.dll',base.AMXX/'modules/goldcraft_amxx.dll')
        self.copy(ROOT/'build/amxx/plugins/goldcraft.amxx',base.AMXX/'plugins/goldcraft.amxx')
        self.copy(ROOT/'build/amxx/plugins/goldcraft_melee_test.amxx',base.AMXX/'plugins/goldcraft_melee_test.amxx')
        self.copy(self.server_dll,base.GAME/'cstrike/dlls/mp.dll')
        self.put(base.GAME/'cstrike/game_init.cfg',b'mc_map_mining 0\n')
        self.put(base.GAME/'cstrike/server.cfg',(base.GAME/'cstrike/server.cfg').read_bytes()+b'exec goldcraft_zp_reapi.cfg\nmc_melee_test 1\nzp_gamemode_delay 100000\n')
        for p in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log']:self.put(p,b'')

    def run(self):
        self.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(self.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(self.output/'bridge.log'))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=subprocess.SW_HIDE
        self.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(self.config['csPort']),'-maxplayers','4','+map','cs_assault'],cwd=base.GAME,env=env,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report['pid']=self.process.pid;print(json.dumps(dict(ownedFixturePid=self.process.pid)),flush=True)
        self.ready()
        def connect():self.peer=MeleePeer(self.config);return self.peer
        self.until(connect,'Bridge listening');self.until(lambda:self.peer.world,'World')
        self.check('Capability accepted',self.peer.capabilities==CAP)
        self.report['plugins']=self.command('amxx plugins');self.report['modules']=self.command('amxx modules')
        self.report['amxxVersion']=self.command('amxx version')
        self.report['goldcraftApi']=self.command('gc_amxx_status')
        self.check('Pinned AMXX 1.10 runtime loaded','1.10.0.5487' in self.report['amxxVersion'])
        self.check('GoldCraft and ReAPI natives callable',
                   bool(re.search(r'api=[1-9]\d* rehlds=1 regamedll=1',self.report['goldcraftApi'])))
        self.check('Floating Damage production plugin loaded once',
                   self.report['plugins'].count('1.0.7-gc')==1 and '3 plugins, 3 running' in self.report['plugins'])
        self.slot=int(self.action('create')['attacker']);self.command('sv_restart 1');self.wait(1.5)
        self.configure();normal=self.action('attack 0 0')
        self.check('Actual knife trace hits victim',normal['traces']==1 and normal['trace']>0 and normal['health']<1000)
        self.configure(3);strong=self.action('attack 0 0')
        self.check('Strength adds 15 native units before TraceAttack',abs(strong['trace']-normal['trace']-15)<.01)
        self.configure(-4);weak=self.action('attack 0 0')
        self.check('Weakness subtracts 20 without clamped proxy base',abs(weak['trace']-max(0,normal['trace']-20))<.01)
        self.configure();stab=self.action('attack 1 0');back=self.action('attack 2 0')
        self.configure(3);strong_stab=self.action('attack 1 0');strong_back=self.action('attack 2 0')
        self.check('Secondary knife strength before backstab multiplier',abs(strong_stab['trace']-stab['trace']-15)<.01 and abs(strong_back['trace']/strong_stab['trace']-back['trace']/stab['trace'])<.01)
        armored=self.action('attack 0 100')
        self.check('Native armor still absorbs the modified hit',armored['health']>strong['health'] and armored['armor']<100)
        cancelled=self.action('attack 3 0')
        self.check('ReAPI pre hook still cancels modified damage',cancelled['health']==1000 and cancelled['traces']==1)
        self.peer.configure_melee(None);self.wait(.45);expired=self.action('attack 0 0')
        self.check('Lease expiry restores exact native knife',expired['trace']==normal['trace'])
        self.configure(3);self.peer.close();self.peer=None;disconnected=self.action('attack 0 0')
        self.check('Authority disconnect restores native knife',disconnected['trace']==normal['trace'])
        errors={}
        for p in (base.AMXX/'logs').glob('error_*.log'):
            extra=p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            if extra.strip():errors[p.name]=extra
        self.report['runtimeErrors']=errors;self.check('No Pawn runtime errors',not errors);self.report['passed']=True

    def close(self,output=None):
        if self.peer:self.peer.close();self.peer=None
        super().close(output)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--name',default='run-'+str(time.time_ns()))
    parser.add_argument('--server-dll',type=Path);args=parser.parse_args()
    if Path(args.name).name!=args.name:parser.error('Single output directory name required')
    out=ROOT/'build/native-melee-20261018'/args.name
    if any(p.is_symlink() or p.is_junction() for p in [out,*out.parents]):raise ValueError('Linked output refused')
    out.mkdir(parents=True,exist_ok=False);base.FIXTURE_LOG=out/'server.log';base.FIXTURE_LOG.write_text('')
    run=Run(out,args.server_dll)
    try:run.run()
    except Exception as exc:run.report.update(error=str(exc),passed=False);raise
    finally:
        logs=[p.read_text('utf-8',errors='replace') for p in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log'] if p.exists()]
        base.FIXTURE_LOG.write_text('\n'.join(logs).replace(run.config['csRconToken'],'[redacted]'),encoding='utf-8')
        run.close(out/'result.json')
    raise SystemExit(0 if run.report['passed'] and run.report['fixtureFilesRestored'] and run.report['fixtureStopped'] else 1)
