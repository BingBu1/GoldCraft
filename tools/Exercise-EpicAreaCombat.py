from pathlib import Path
import argparse,importlib.util,json,os,subprocess,sys,time,hashlib,math
parser=argparse.ArgumentParser(description='Run real Epic/native damage checks on independent servers; no graphical clients.')
parser.add_argument('--instance',choices=['headless-combat','epic-area-combat'],default='headless-combat')
parser.add_argument('--regression',action='store_true')
parser.add_argument('--ai',action='store_true')
args=parser.parse_args()
ROOT=Path(__file__).resolve().parent.parent;HERE=ROOT/'analysis/epic-area-combat';HERE.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'tools'));sys.stdout.reconfigure(encoding='utf-8')
spec=importlib.util.spec_from_file_location('area_headless',ROOT/'tools/Exercise-HeadlessCombat.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
base.TEST=ROOT/'sandbox'/args.instance
class Run(base.CombatRun):
    def __init__(self):
        super().__init__()
        self.report['scope']='Real Epic/NightFall skill methods and native CBasePlayer on independent servers; no graphical client input.'
        for path in (base.TEST/'minecraft/mods').glob('*.jar'):
            self.report['artifacts'][str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest()
    command_at=0
    def state(self,minecraft_server=False):
        for attempt in range(20):
            try:return super().state(minecraft_server)
            except (PermissionError,json.JSONDecodeError):
                if attempt==19:raise
                time.sleep(.03)
    def cs(self,command,record=True):
        time.sleep(max(0,self.command_at+.8-time.monotonic()));self.command_at=time.monotonic()
        return super().cs(command,record)
    def run(self):
        game=base.TEST/'Half-Life'
        self.launch('ReHLDS',game/'hlds.exe',['-game','cstrike','-insecure','-nomaster','-console','+ip','127.0.0.1','-port',str(self.config['csPort']),'-maxplayers','4','+sv_lan','0','+map','cs_assault','+exec','server.cfg'],game)
        self.wait(lambda:'ReHLDS' in self.cs('version',False),'ReHLDS RCON',60)
        self.cs('sv_lan 0');self.cs('mp_friendlyfire 0')
        manifest=json.loads((ROOT/'build/neoforge-production/runServer.json').read_text('utf-8'))
        self.launch('Minecraft',ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe',['@'+str(base.TEST/'server.args')],base.TEST/'minecraft',manifest.get('environment'),manifest.get('clearEnvironment',[]))
        self.wait(lambda:'players online' in self.mc('list',False),'MC RCON',120)
        self.wait(lambda:self.state(True)['map']=='cs_assault','Authenticated Minecraft/native bridge',40)
        self.mc('gamerule doMobSpawning false');self.mc('gamerule doDaylightCycle false');self.mc('time set midnight');self.mc('difficulty normal')
        self.mc(f'execute in {base.DIM} run forceload add -16 -16 47 15')
        self.cs('gc_headless_create')
        self.slot=int(self.wait(lambda:(d if (d:=self.fixture()).get('alive')==1 else None),'Native fake client')['slot'])
        self.reset_player()
        if '--ai' in sys.argv:
            self.mc(f'execute in {base.DIM} run kill @e[type=minecraft:zombie]')
            self.clear_mobs()
            self.spawn('zombie');start=self.position()
            self.wait(lambda:self.fixture().get('applied',0)>0,'Natural Epic zombie attack',35)
            zombie=self.capture('zombie-proactive')
            self.check('Epic zombie naturally selects and attacks native player',math.dist(start,self.position())>1.5 and zombie['fixture']['health']<100)
            self.report['outcome']='PASSED'
            return
        if '--regression' in sys.argv:
            self.regression()
            return
        self.check('Only one native spatial target', 'spatialNativeTargets=1' in self.mc('area_prepare 6'))
        self.check('Logical body resolves UUID without duplicate spatial entity','uuidLookup=true playerLookup=true spatialBody=false' in self.mc('area_identity'))
        self.wait(lambda:self.state()['minecraftObjects']>0,'Attacker exported')
        def counters():
            n=self.state(True)['nativePlayers'];return n['damageRequests'],n['damageAccepted'],n['damageRejected']
        for distance in [2,6,12]:
            for skill in ['normal','perfect']:
                self.reset_player();self.mc('area_prepare '+str(distance));time.sleep(.4)
                initial=counters();before=self.fixture()['health'];results=[]
                for pulse in range(3):
                    results.append(self.mc('area_fire '+skill));time.sleep(.45)
                after=self.wait(lambda:(n if (n:=counters())[1]+n[2]>=initial[1]+initial[2]+3 else None),'Three native damage acknowledgements',12)
                health=self.fixture()['health']
                self.report.setdefault('skills',[]).append(dict(distance=distance,skill=skill,initial=initial,after=after,healthBefore=before,healthAfter=health,results=results))
                self.check(f'{skill} at {distance} blocks hits exactly once per pulse',after[0]-initial[0]==3 and after[1]-initial[1]==3 and after[2]==initial[2])
                self.check(f'{skill} at {distance} blocks native health',abs(health-(before-75))<.01)
        self.reset_player();self.mc('area_prepare 6');time.sleep(.4);initial=counters();before=self.fixture()['health'];self.mc('area_fire plain');time.sleep(.65)
        after=counters();self.check('Vanilla melee keeps four block restriction',after[2]==initial[2]+1 and self.fixture()['health']==before)
        for skill in ['mortal1','mortal2','endcut']:
            self.reset_player();self.mc('area_prepare 6');time.sleep(.4);initial=counters();before=self.fixture()['health'];result=self.mc('area_fire '+skill);time.sleep(.7);after=counters();health=self.fixture()['health']
            self.report.setdefault('mortal',[]).append(dict(skill=skill,result=result,initial=initial,after=after,healthBefore=before,healthAfter=health))
            self.check(skill+' real collider and attack reaches native damage',after[1]>initial[1] and health<before)
        self.reset_player();self.mc('area_prepare 6');time.sleep(.4)
        self.mc(f'execute in {base.DIM} run fill 12 64 -7 12 67 -4 minecraft:bedrock');time.sleep(.6)
        initial=counters();before=self.fixture()['health'];self.mc('area_fire normal');time.sleep(.65);after=counters()
        self.check('Native trace still rejects skill behind Minecraft wall',after[2]==initial[2]+1 and self.fixture()['health']==before)
        self.report['outcome']='PASSED'
    def regression(self):
        self.mc(f'execute in {base.DIM} run fill 12 64 -7 12 67 -4 minecraft:air')
        self.reset_player(2000)
        self.mc('area_prepare 6');time.sleep(.5)
        initial=self.state(True)['nativePlayers'];before=self.fixture()['health']
        result=self.mc('area_fire endcut');time.sleep(.7)
        after=self.state(True)['nativePlayers'];health=self.fixture()['health']
        self.report['bossEndcut']=dict(initial=initial,after=after,before=before,health=health,result=result)
        self.check('Boss endcut commits both real strikes through native authority',
            after['damageAccepted']-initial['damageAccepted']==2 and health<before-15
            and 'localHealthBefore=400.0 localHealthAfter=400.0' in result)
        self.mc(f'execute in {base.DIM} run kill @e[type=minecraft:zombie]')
        self.wait(lambda:self.state()['minecraftObjects']==0,'Skill owner removed')
        self.reset_player()
        proxy=self.spawn('wolf');start=self.position();before=self.health()
        self.cs(f'gc_headless_fire {proxy["slot"]} 5')
        self.wait(lambda:0<self.health()<before,'Native bullet damages real wolf')
        self.check('Native bullet keeps vanilla anger and attacker UUID',self.wait(lambda:self.anger()>0,'Vanilla anger',10))
        self.wait(lambda:self.fixture().get('applied',0)>0,'Natural wolf retaliation',35)
        wolf=self.capture('wolf-retaliation')
        self.check('Wolf naturally chases logical body and damages native health',
            math.dist(start,self.position())>1.5 and wolf['fixture']['health']<100
            and wolf['fixture']['source']=='goldcraft_object')
        self.reset_player(100,100)
        self.wait(lambda:self.fixture().get('applied',0)>0,'Armored wolf bite',20)
        armor=self.capture('armor')
        self.check('ReGameDLL armor remains authoritative',armor['fixture']['health']<100 and armor['fixture']['armor']<100)
        life=self.actor()['life'];self.reset_player(5)
        self.wait(lambda:self.fixture().get('deaths',0)==1,'Native lethal wolf bite',20)
        self.wait(lambda:not self.actor()['alive'] and self.state(True)['nativePlayers']['players']==0,'Dead native target removal')
        self.check('Native death removes AI body exactly once',self.fixture()['deaths']==1)
        self.clear_mobs();self.reset_player()
        self.check('Native respawn advances life identity',self.actor()['life']>life)
        self.spawn('zombie');start=self.position()
        self.wait(lambda:self.fixture().get('applied',0)>0,'Natural zombie attack after respawn',35)
        zombie=self.capture('zombie-proactive')
        self.check('Epic zombie naturally selects and attacks respawned native player',
            math.dist(start,self.position())>1.5 and zombie['fixture']['health']<100)
        self.report['outcome']='PASSED'
    def finish(self):
        for name,command in [('Minecraft',lambda:self.mc('stop')),('ReHLDS',lambda:self.cs('quit'))]:
            p=self.processes.get(name)
            if p is None:continue
            if p.poll() is None:
                try:command()
                except Exception:pass
                try:p.wait(timeout=30)
                except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=10)
            self.report['processes'][name]['exitCode']=p.returncode
        for f in self.files:f.close()
        path=HERE/('integrated-result-'+self.stamp+'.json');path.write_text(self.clean(json.dumps(self.report,indent=2,ensure_ascii=False)),'utf-8')
        print(json.dumps(dict(report=str(path),outcome=self.report.get('outcome','INCOMPLETE'),error=self.report.get('error'))),flush=True)
run=Run()
try:run.run()
except Exception as e:
    run.report['outcome']='INCOMPLETE';run.report['error']=run.clean(repr(e))
    for name,mc in [('native',False),('minecraft',True)]:
        try:run.report['failure_'+name]=run.state(mc)
        except Exception:pass
finally:run.finish()
raise SystemExit(0 if run.report.get('outcome')=='PASSED' else 1)
