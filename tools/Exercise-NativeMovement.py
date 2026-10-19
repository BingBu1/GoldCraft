"""Observe production ReHLDS movement snapshots against real engine entvars.

Only the inactive localhost fixture is changed. Pawn pins known engine state;
this checks export/lifecycle, not natural movement or melee knockback gameplay.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('movement_view',ROOT/'tools/Exercise-NativeView.py')
view=importlib.util.module_from_spec(spec);spec.loader.exec_module(view)
base=view.base
CAP=134217728


class Peer(view.melee.AuthorityPeer):
    def __init__(self,config):
        self.movement={};self.tick=0;self.samples=0
        super().__init__(config,bytes(16),CAP)
    def receive(self,kind,body,epoch):
        super().receive(kind,body,epoch)
        if kind==11:self.tick=struct.unpack_from('<Q',body,8)[0]
        if kind!=103:return
        world,tick,count=struct.unpack_from('<QQI',body)
        if world!=self.world or tick!=self.tick or len(body)!=20+32*count:raise AssertionError('Movement batch is not paired with actors')
        states={}
        for i in range(count):
            slot,serial,spawn,life,x,y,z,flags=struct.unpack_from('<4I3fI',body,20+32*i)
            a=self.actors[slot]
            if (serial,spawn,life)!=(a['serial'],a['spawn'],a['life']) or flags&~3:raise AssertionError('Movement identity mismatch')
            states[slot]=dict(serial=serial,spawn=spawn,life=life,velocity=[x,y,z],flags=flags)
        self.movement=states;self.samples+=1


class Run(view.Run):
    def sample_movement(self,velocity,flags):
        previous=self.peer.samples
        def ready():
            s=self.peer.movement.get(self.slot)
            return s if self.peer.samples>previous and s and s['flags']==flags and all(abs(a-b)<.001 for a,b in zip(velocity,s['velocity'])) else None
        return self.until(ready,'Matching engine movement snapshot')
    def run(self):
        self.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(self.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(self.output/'bridge.log'))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=subprocess.SW_HIDE
        self.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(self.config['csPort']),'-maxplayers','4','+map','cs_assault'],cwd=base.GAME,env=env,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report['pid']=self.process.pid;print(json.dumps(dict(ownedFixturePid=self.process.pid)),flush=True);self.ready()
        def connect():self.peer=Peer(self.config);return self.peer
        self.until(connect,'Bridge ready');self.until(lambda:self.peer.world,'World')
        self.check('Only negotiated movement capability',self.peer.capabilities==CAP)
        self.slot=self.action('create')['slot'];self.command('sv_restart 1');self.wait(1.5)
        self.action('pose 496 176 36 1 2 17')
        for velocity,ground,move,flags in [([320,-640,128],0,3,2),([-100,50,-20],1,3,3),([0,0,0],1,0,1),([50,20,30],0,0,0)]:
            self.action('movement '+' '.join(map(str,[*velocity,ground,move])))
            s=self.sample_movement(velocity,flags);self.report['snapshots'].append(dict(movement=s))
            self.check(f'Engine velocity ground={ground} move={move}',True)
        old=s;self.action('kill');self.until(lambda:self.slot not in self.peer.movement,'Dead movement removed');self.check('Death removes record',True)
        self.action('respawn');new=self.sample_movement([50,20,30],0)
        self.check('Respawn gets new life and spawn',new['life']!=old['life'] and new['spawn']!=old['spawn'])
        self.peer.close();self.peer=None;self.wait(.1);connect();self.until(lambda:self.peer.world,'Reconnected world')
        self.sample_movement([50,20,30],0);self.check('Reconnect supplies fresh sample',True)
        errors={}
        for p in (base.AMXX/'logs').glob('error_*.log'):
            extra=p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            if extra.strip():errors[p.name]=extra
        self.report['runtimeErrors']=errors;self.check('No Pawn errors',not errors);self.report['passed']=True


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--name',default='rehlds-'+str(time.time_ns()));args=parser.parse_args()
    if Path(args.name).name!=args.name:parser.error('Single output directory name required')
    out=ROOT/'build/native-movement-20261019'/args.name
    if any(p.is_symlink() or p.is_junction() for p in [out,*out.parents]):raise ValueError('Linked output refused')
    out.mkdir(parents=True,exist_ok=False);base.FIXTURE_LOG=out/'server.log';base.FIXTURE_LOG.write_text('')
    run=Run(out);run.report['scope']=__doc__
    try:run.run()
    except BaseException as exc:run.report.update(passed=False,error=str(exc));raise
    finally:
        logs=[p.read_text('utf-8',errors='replace') for p in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log'] if p.exists()]
        base.FIXTURE_LOG.write_text('\n'.join(logs).replace(run.config['csRconToken'],'[redacted]'),encoding='utf-8')
        run.close(out/'result.json')
    raise SystemExit(0 if run.report['passed'] and run.report['fixtureFilesRestored'] and run.report['fixtureStopped'] else 1)
