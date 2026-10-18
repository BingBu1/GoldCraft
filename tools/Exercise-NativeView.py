"""Verify real ReHLDS eye/water snapshots; MC fluid authority is a TCP fixture."""
from pathlib import Path
import argparse
import importlib.util
import json
import os
import re
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('native_view_harness',ROOT/'tools/Exercise-NativeMelee.py')
melee=importlib.util.module_from_spec(spec);spec.loader.exec_module(melee)
base=melee.base
CAP=8388608|32


class ViewPeer(melee.AuthorityPeer):
    def __init__(self,config):
        self.fluid=None;self.fluid_revision=0;self.views={};self.snapshot_tick=0;self.samples=0
        super().__init__(config,bytes(16),CAP)
    def publish(self):
        if self.fluid is None:return
        self.fluid_revision+=1
        body=struct.pack('<QQI',self.world,self.fluid_revision,len(self.fluid))
        for contents,lo,hi in self.fluid:body+=struct.pack('<i6f',contents,*lo,*hi)
        self.send(79,body)
    def receive(self,kind,body,epoch):
        super().receive(kind,body,epoch)
        if kind==11:self.snapshot_tick=struct.unpack_from('<Q',body,8)[0]
        if kind!=99:return
        world,tick,count=struct.unpack_from('<QQI',body)
        if world!=self.world or tick!=self.snapshot_tick or len(body)!=20+32*count:
            raise AssertionError('Eye packet is not bound to its actor snapshot')
        views={}
        for i in range(count):
            slot,serial,spawn,life,x,y,z,flags=struct.unpack_from('<4I3fI',body,20+32*i)
            a=self.actors[slot]
            if (serial,spawn,life)!=(a['serial'],a['spawn'],a['life']) or flags not in (0,1):
                raise AssertionError('Eye actor identity or flags mismatch')
            views[slot]=dict(serial=serial,spawn=spawn,life=life,eye=[x,y,z],water=bool(flags))
        self.views=views;self.samples+=1


class Run(melee.Run):
    def prepare(self):
        super().prepare()
        self.copy(ROOT/'build/amxx/plugins/goldcraft_native_view_test.amxx',base.AMXX/'plugins/goldcraft_native_view_test.amxx')
        self.put(base.AMXX/'configs/plugins.ini',b'goldcraft_native_view_test.amxx debug\n')
        self.put(base.GAME/'cstrike/server.cfg',(base.GAME/'cstrike/server.cfg').read_bytes()+b'mc_native_view_test 1\n')
    def action(self,text):
        response=self.command('gc_native_view '+text)
        found=re.search(r'\[GC View\] slot=(\d+) eye=([\d.,-]+) contents=(-?\d+) alive=(\d+)',response)
        if not found:raise AssertionError('Missing real Pawn view response: '+text)
        result=dict(slot=int(found[1]),eye=list(map(float,found[2].split(','))),contents=int(found[3]),alive=int(found[4]))
        self.report['snapshots'].append(dict(action=text,state=result));return result
    def sample(self,water,eye=None):
        previous=self.peer.samples
        def ready():
            v=self.peer.views.get(self.slot)
            return v if self.peer.samples>previous and v and v['water']==water and (eye is None or all(abs(a-b)<.001 for a,b in zip(eye,v['eye']))) else None
        return self.until(ready,'Matching water/eye snapshot')
    def fluids(self,contents,top):
        with self.peer.lock:self.peer.fluid=[] if contents is None else [(contents,(480,160,0),(520,200,top))]
    def run(self):
        self.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(self.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(self.output/'bridge.log'))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=subprocess.SW_HIDE
        self.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(self.config['csPort']),'-maxplayers','4','+map','cs_assault'],cwd=base.GAME,env=env,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
        self.report['pid']=self.process.pid;print(json.dumps(dict(ownedFixturePid=self.process.pid)),flush=True);self.ready()
        def connect():self.peer=ViewPeer(self.config);return self.peer
        self.until(connect,'Bridge ready');self.until(lambda:self.peer.world,'World')
        self.check('Both capabilities accepted',self.peer.capabilities==CAP)
        self.slot=self.action('create')['slot'];self.command('sv_restart 1');self.wait(1.5)
        state=self.action('pose 496 176 36 1 2 17');self.sample(False,[497,178,53])
        self.check('Actual custom view offset exported',state['eye']==[497,178,53] and state['contents']==-1)
        self.fluids(-3,32);self.wait(.15);self.sample(False);self.check('MC shallow water does not submerge eyes',True)
        self.fluids(-3,128);self.sample(True);self.check('MC deep water reaches actual eye',True)
        self.action('pose 496 176 36 1 2 92');self.sample(False,[497,178,128]);self.check('Exact MC fluid surface excluded',True)
        self.action('pose 496 176 36 1 2 91.5');self.sample(True,[497,178,127.5]);self.check('Immediately below surface included',True)
        self.fluids(-5,128);self.sample(False);self.check('MC lava does not enable conduit',True)
        self.fluids(-3,128);self.sample(True)
        state=self.action('brush -5');self.check('Real native lava brush point contents',state['contents']==-5)
        self.sample(False);self.check('Native lava takes priority over MC water',True)
        self.fluids(-5,128);state=self.action('brush -3');self.check('Real native water brush point contents',state['contents']==-3)
        self.sample(True);self.check('Native water takes priority over MC lava',True)
        self.action('brush 0');self.sample(False)
        self.fluids(-3,128);self.sample(True)
        with self.peer.lock:self.peer.fluid=None
        self.wait(1.2);self.sample(False);self.check('Expired authority fluid field removed',True)
        # The pinned map's floor is solid. Water cannot illuminate through it.
        self.fluids(-3,128);state=self.action('pose 496 176 36 1 2 -40')
        self.check('Actual world BSP solid point',state['contents']==-2);self.sample(False,[497,178,-4])
        self.check('Native solid takes priority over MC water',True)
        self.action('pose 496 176 36 1 2 17');old=self.sample(True)
        self.action('kill');self.until(lambda:self.slot not in self.peer.views,'Dead player eye removed');self.check('Death removes eye record',True)
        self.action('respawn');new=self.sample(True);self.check('Respawn exports new identity',new['life']!=old['life'] and new['spawn']!=old['spawn'])
        count=self.peer.samples;self.peer.close();self.peer=None;self.wait(.1)
        connect();self.until(lambda:self.peer.world,'Reconnected world');self.sample(False,[497,178,53]);self.check('Reconnect cannot inherit prior fluid field',True)
        self.report['samplesBeforeReconnect']=count;self.report['samplesAfterReconnect']=self.peer.samples
        errors={}
        for p in (base.AMXX/'logs').glob('error_*.log'):
            extra=p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            if extra.strip():errors[p.name]=extra
        self.report['runtimeErrors']=errors;self.check('No Pawn runtime errors',not errors);self.report['passed']=True


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--name',default='rehlds-'+str(time.time_ns()));args=parser.parse_args()
    if Path(args.name).name!=args.name:parser.error('Single output directory name required')
    out=ROOT/'build/conduit-vision-20261018'/args.name
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
