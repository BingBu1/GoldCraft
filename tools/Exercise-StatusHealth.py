"""Exercise the production health-limit consumer in isolated ReHLDS/ZP.

The authenticated MC peer is a protocol fixture, not a Minecraft JVM. The real
native DLL, ReAPI hooks, Pawn VM and TakeHealth/death/respawn paths run unchanged.
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
CAP=131072


class HealthPeer(AuthorityPeer):
    def __init__(self,config):
        self.limits={};self.desired=None;self.repeating=False;self.last_health=None
        super().__init__(config,bytes(16),CAP)

    def receive(self,kind,body,epoch):
        super().receive(kind,body,epoch)
        if kind==92:
            world,tick,count=struct.unpack_from('<QQI',body)
            if world!=self.world or len(body)!=20+32*count:raise ValueError('Invalid health snapshot')
            result={}
            for n in range(count):
                slot,serial,spawn,life,baseline,effective,revision=struct.unpack_from('<4I2fQ',body,20+32*n)
                result[slot]=dict(world=world,tick=tick,slot=slot,serial=serial,spawn=spawn,life=life,base=baseline,effective=effective,revision=revision)
            self.limits=result

    def configure_limit(self,record,effective=None,repeat=False):
        with self.lock:
            self.desired=(dict(record),effective) if record else None
            self.repeating=repeat

    def publish(self):
        if not self.desired:return
        if self.repeating:
            if self.last_health:self.send(93,self.last_health)
            return
        record,effective=self.desired;self.revision+=1
        self.last_health=struct.pack('<QQ4I2f',record['world'],self.revision,record['slot'],record['serial'],record['spawn'],record['life'],record['base'],effective)
        self.send(93,self.last_health)


class Run(base.Run):
    def __init__(self,output):
        super().__init__();self.output=output;self.peer=None;self.command_at=0
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
        output=self.command('gc_status_health '+text)
        lines=re.findall(r'\[GC StatusHealth\] ([^\r\n]+)',output)
        if not lines:raise ValueError('Missing Pawn action response: '+text)
        values={key:float(value) for key,value in re.findall(r'(\w+)=(-?[\d.]+)',lines[-1])}
        self.report['snapshots'].append(dict(action=text,state=values));return values

    def record(self,baseline=None):
        return self.until(lambda:(r:=self.peer.limits.get(self.slot)) and (baseline is None or r['base']==baseline) and r,'Current native limit')

    def configure(self,effective):
        self.peer.configure_limit(self.record(),effective)
        return self.until(lambda:(r:=self.peer.limits.get(self.slot)) and r['effective']==effective and r['revision']>0 and r,'Effective limit acknowledged')

    def prepare(self):
        super().prepare();self.put(base.TEST/'cluster.json',(json.dumps(self.config)+'\n').encode())
        for p in (base.AMXX/'configs').glob('plugins-*.ini'):self.put(p,b'; isolated health fixture\n')
        lines=(base.AMXX/'configs/plugins.ini').read_text().splitlines()
        lines=[s for s in lines if not s.startswith('goldcraft_zp_reapi_test.')]
        lines.append('goldcraft_status_health_test.amxx debug')
        self.put(base.AMXX/'configs/plugins.ini',('\n'.join(lines)+'\n').encode())
        self.copy(ROOT/'build/amxx/plugins/goldcraft_status_health_test.amxx',base.AMXX/'plugins/goldcraft_status_health_test.amxx')
        self.copy(ROOT/'build/regamedll/Release/mp.dll',base.GAME/'cstrike/dlls/mp.dll')
        self.put(base.GAME/'cstrike/game_init.cfg',b'mc_map_mining 0\n')
        self.put(base.GAME/'cstrike/server.cfg',(base.GAME/'cstrike/server.cfg').read_bytes()+b'exec goldcraft_zp_reapi.cfg\nmc_status_health_test 1\nzp_gamemode_delay 100000\n')
        for p in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log']:self.put(p,b'')

    def run(self):
        self.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(self.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(self.output/'bridge.log'))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=subprocess.SW_HIDE
        self.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(self.config['csPort']),'-maxplayers','4','+map','cs_assault'],cwd=base.GAME,env=env,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report['pid']=self.process.pid;print(json.dumps(dict(ownedFixturePid=self.process.pid)),flush=True)
        self.ready()
        def connect():self.peer=HealthPeer(self.config);return self.peer
        self.until(connect,'Bridge listening');self.until(lambda:self.peer.world,'World')
        self.check('Capability accepted',self.peer.capabilities==CAP)
        self.report['plugins']=self.command('amxx plugins');self.report['modules']=self.command('amxx modules')
        self.slot=int(self.action('create')['slot']);self.command('sv_restart 1');self.wait(1.5)
        before=self.action('set 100 100');self.record(100)
        self.configure(120);after=self.action('heal 500')
        self.check('Real TakeHealth uses effective limit without changing class base',after['health']==120 and after['base']==100 and after['heals']>before['heals'])
        self.configure(100);after=self.action('status')
        self.check('Effect removal caps health without damage or death',after['health']==100 and after['damage']==before['damage'] and after['deaths']==before['deaths'])
        self.configure(120);self.action('set 100 170');self.configure(100)
        self.check('Native reward above previous limit survives removal',self.action('status')['health']==150)
        self.peer.configure_limit(None);self.action('set 12000 12000');self.record(12000);self.configure(12040)
        after=self.action('heal 500')
        self.check('High class heals above vanilla attribute ceiling',after['health']==12040 and after['base']==12000)
        self.configure(12000);self.check('High class returns to own base',self.action('status')['health']==12000)
        self.peer.configure_limit(None);self.action('set 100 100');self.record(100);self.configure(120);self.action('heal 100')
        self.peer.configure_limit(self.record(),120,repeat=True);self.wait(.45)
        self.until(lambda:self.record()['revision']==0,'Lease expired')
        after=self.action('heal 10');self.check('Repeated revision expires without removing reward HP',after['health']==120 and after['base']==100)
        self.peer.configure_limit(None);self.action('set 100 70');self.record(100);self.configure(120)
        self.action('set 200 180')
        self.wait(.15);self.check('Old class in-flight state cannot change new healing cap',self.action('heal 100')['health']==200)
        self.peer.configure_limit(None);self.record(200);self.configure(240)
        old=dict(self.record());self.action('kill');self.action('respawn');self.action('set 200 180')
        self.peer.configure_limit(old,240);self.wait(.15)
        self.check('Old spawn cannot change respawn healing cap',self.action('heal 100')['health']==200)
        self.peer.configure_limit(None);self.record(200);self.configure(240)
        self.peer.close();self.peer=None
        self.action('set 200 180');self.check('Disconnected MC authority restores native base healing',self.action('heal 100')['health']==200)
        errors={}
        for p in (base.AMXX/'logs').glob('error_*.log'):
            extra=p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            if extra.strip():errors[p.name]=extra
        self.report['runtimeErrors']=errors;self.check('No Pawn runtime errors',not errors);self.report['passed']=True

    def close(self,output=None):
        if self.peer:self.peer.close();self.peer=None
        super().close(output)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--name',default='run-'+str(time.time_ns()));args=parser.parse_args()
    if Path(args.name).name!=args.name:parser.error('Single output directory name required')
    out=ROOT/'build/status-health-native-20261017'/args.name
    if any(p.is_symlink() or p.is_junction() for p in [out,*out.parents]):raise ValueError('Linked output refused')
    out.mkdir(parents=True,exist_ok=False);base.FIXTURE_LOG=out/'server.log';base.FIXTURE_LOG.write_text('')
    run=Run(out)
    try:run.run()
    except Exception as exc:run.report.update(error=str(exc),passed=False);raise
    finally:
        logs=[p.read_text('utf-8',errors='replace') for p in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log'] if p.exists()]
        base.FIXTURE_LOG.write_text('\n'.join(logs).replace(run.config['csRconToken'],'[redacted]'),encoding='utf-8')
        run.close(out/'result.json')
    raise SystemExit(0 if run.report['passed'] and run.report['fixtureFilesRestored'] and run.report['fixtureStopped'] else 1)
