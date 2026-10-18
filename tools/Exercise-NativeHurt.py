"""Real ReHLDS settlement notifications; authenticated MC peer is a protocol fixture."""
from pathlib import Path
import argparse,importlib.util,json,os,struct,subprocess,time

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('native_melee',ROOT/'tools/Exercise-NativeMelee.py')
melee=importlib.util.module_from_spec(spec);spec.loader.exec_module(melee)
base=melee.base
CAP=16777216|16

class Peer(melee.AuthorityPeer):
    def __init__(self,config):
        self.hits=[];self.frames=[];self.attributes=None;self.spends=[]
        super().__init__(config,bytes(16),CAP)
    def publish(self):
        if self.attributes:
            a,pool=self.attributes;self.revision+=1
            self.send(77,struct.pack('<4Q5I4f',self.world,self.revision,0,0,a['slot'],a['serial'],a['spawn'],a['life'],0,1,1,pool,0))
    def receive(self,kind,body,epoch):
        if kind==100:
            values=struct.unpack('<3Q8I4f',body)
            self.hits.append(dict(zip(['world','event','tick','slot','serial','spawn','life','attacker','attackerSerial','attackerSpawn','kind','amount','x','y','z'],values)))
        if kind==78:self.spends.append(body)
        self.frames.append((kind,len(self.hits)))
        super().receive(kind,body,epoch)
    def pay(self,body,paid):
        world,event=struct.unpack_from('<QQ',body);slot,serial,spawn=struct.unpack_from('<3I',body,32)
        self.send(78,struct.pack('<QQ3If',world,event,slot,serial,spawn,paid))

class Run(melee.Run):
    def run(self):
        self.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(self.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(self.output/'bridge.log'))
        self.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug',
            '+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(self.config['csPort']),'-maxplayers','4','+map','cs_assault'],
            cwd=base.GAME,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
        self.report['pid']=self.process.pid;print(json.dumps(dict(ownedFixturePid=self.process.pid)),flush=True)
        self.ready()
        def connect():self.peer=Peer(self.config);return self.peer
        self.until(connect,'Bridge listening');self.until(lambda:self.peer.world,'World')
        self.check('Negotiated native hit extension',self.peer.capabilities==CAP)
        self.report['amxxVersion']=self.command('amxx version')
        self.check('AMXX 1.10 loaded','1.10.0.5487' in self.report['amxxVersion'])
        created=self.action('create');victim=int(created['victim']);attacker=int(created['attacker'])
        self.command('sv_restart 1');self.wait(1.5)
        self.until(lambda:victim in self.peer.actors,'Victim identity snapshot')
        for action,kind in [('attack 0 0',1),('attack 1 0',1),('attack 0 100',1),*[(f'hurt {n} 0',n) for n in range(8)]]:
            count=len(self.peer.hits);state=self.action(action)
            self.until(lambda:len(self.peer.hits)>count,action+' native notification');self.wait(.08)
            hit=self.peer.hits[-1];actor=self.peer.actors[victim]
            self.check(action+' emits once',len(self.peer.hits)==count+1)
            self.check(action+' identity and source',hit['slot']==victim and hit['serial']==actor['serial'] and hit['spawn']==actor['spawn']
                       and hit['kind']==kind and hit['attacker']==(attacker if kind in (1,2,3) else 0))
            self.check(action+' settled amount',state['health']<1000 and abs((1000-state['health'])-hit['amount'])<1)
        for action in ['attack 3 0','hurt 9 0']:
            count=len(self.peer.hits);state=self.action(action);self.wait(.15)
            self.check(action+' rejected without notification',state['health']==1000 and len(self.peer.hits)==count)
        count=len(self.peer.hits);self.action('hurt 8 0');self.until(lambda:len(self.peer.hits)>count,'Fatal notification');self.wait(.1)
        self.check('Fatal hit emits once',len(self.peer.hits)==count+1 and self.peer.hits[-1]['amount']==25)
        # Real host absorption bookkeeping; MC payment is explicitly a fixture.
        self.action('hurt 1 0');self.wait(.1)
        with self.peer.lock:self.peer.attributes=(dict(self.peer.actors[victim]),5)
        self.wait(.15);count=len(self.peer.hits);spends=len(self.peer.spends);state=self.action('hurt 1 0')
        self.until(lambda:len(self.peer.spends)>spends,'Absorption debit');self.wait(.1)
        self.check('Absorbed hit waits for payment',state['health']==1000 and len(self.peer.hits)==count)
        with self.peer.lock:self.peer.attributes=None
        payment=self.peer.spends[-1];self.peer.pay(payment,5)
        self.until(lambda:len(self.peer.hits)>count,'Paid absorption notification');self.peer.pay(payment,5);self.wait(.15)
        self.check('Paid absorption emits once',len(self.peer.hits)==count+1 and self.peer.hits[-1]['amount']==25)
        self.report['nativeHits']=self.peer.hits
        errors={}
        for p in (base.AMXX/'logs').glob('error_*.log'):
            extra=p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            if extra.strip():errors[p.name]=extra
        self.report['runtimeErrors']=errors;self.check('No Pawn errors',not errors)
        self.report.update(passed=True,scope=__doc__)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--name',default='run-'+str(time.time_ns()))
    parser.add_argument('--server-dll',type=Path);args=parser.parse_args()
    if Path(args.name).name!=args.name:parser.error('Single output directory name required')
    out=ROOT/'build/native-hit-notify-20261018'/args.name
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
