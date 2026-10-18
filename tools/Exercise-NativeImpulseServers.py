"""Observe real NeoForge wind explosions moving a native ReHLDS player.

Only players and the wind source are fixtures. A byte-preserving relay observes
production actor and impulse messages; no client UI or graphical acceptance.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,struct,subprocess,time

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('hurt_servers',ROOT/'tools/Exercise-NativeHurtServers.py')
dual=importlib.util.module_from_spec(spec);spec.loader.exec_module(dual)
base=dual.base

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--name',required=True);p.add_argument('--jar',type=Path,required=True);p.add_argument('--server-dll',type=Path,required=True);args=p.parse_args()
    if Path(args.name).name!=args.name:raise ValueError('Single output name required')
    out=ROOT/'build/native-impulse-20261018'/args.name
    if any(x.is_symlink() or x.is_junction() for x in [out,*out.parents]):raise ValueError('Linked output refused')
    out.mkdir(exist_ok=False);base.FIXTURE_LOG=out/'server.log';base.FIXTURE_LOG.write_text('')
    run=dual.native.Run(out,args.server_dll);run.report['scope']=__doc__;mc=relay=log=None
    def observed():
        try:return json.loads((out/'minecraft/native-impulse-live.json').read_text())
        except (FileNotFoundError,json.JSONDecodeError):return None
    def command(mode):
        before=observed();mc.stdin.write(f'gc_impulse_probe {victim} {mode}\n'.encode());mc.stdin.flush()
        run.until(lambda:observed() and observed()['mode']==mode and (not before or observed()['commands']>before['commands']),'MC wind command',seconds=90)
        return observed()
    def all_impulses():return [(t,struct.unpack('<3Q4I6f',b)) for t,d,k,b in relay.frames if d=='mc' and k==101]
    def impulses():return [(t,q) for t,q in all_impulses() if q[3]==victim]
    def actors(since):
        result=[]
        for t,d,k,b in relay.frames:
            if t<since or d!='host' or k!=11:continue
            count=struct.unpack_from('<I',b,24)[0]
            for n in range(count):
                offset=28+n*116
                if struct.unpack_from('<I',b,offset)[0]==victim:
                    result.append(dict(time=t,health=struct.unpack_from('<f',b,offset+24)[0],origin=struct.unpack_from('<3f',b,offset+32),velocity=struct.unpack_from('<3f',b,offset+44)))
        return result
    try:
        run.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(run.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(out/'bridge.log'))
        start=subprocess.STARTUPINFO();start.dwFlags|=subprocess.STARTF_USESHOWWINDOW;start.wShowWindow=subprocess.SW_HIDE
        run.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(run.config['csPort']),'-maxplayers','4','+map','cs_assault'],cwd=base.GAME,env=env,startupinfo=start,creationflags=subprocess.CREATE_NEW_CONSOLE)
        run.report['pid']=run.process.pid;run.ready()
        run.report['serverDllSha256']=hashlib.sha256((base.GAME/'cstrike/dlls/mp.dll').read_bytes()).hexdigest()
        run.check('Requested native candidate loaded',run.report['serverDllSha256']==hashlib.sha256(args.server_dll.read_bytes()).hexdigest())
        relay=dual.Relay(run.config['serverPort'],(11,101))
        java,argfile,env=dual.prepare_mc(out/'minecraft',args.jar.resolve(),relay.port,run.config,ROOT/'tests/native-impulse/NativeImpulseServersProbe.java','goldcraft_native_impulse_servers_probe')
        log=(out/'minecraft/runtime.log').open('wb');mc=subprocess.Popen([str(java),'@'+str(argfile)],cwd=out/'minecraft',env=env,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        run.report['minecraftPid']=mc.pid;print(json.dumps(dict(nativePid=run.process.pid,minecraftPid=mc.pid)),flush=True)
        victim=int(run.action('create')['victim']);run.command('sv_restart 1');run.wait(1.5)
        run.until(lambda:'Fresh host map session:' in (out/'minecraft/runtime.log').read_text('utf-8',errors='replace'),'MC authoritative world ready',seconds=90)
        def ready():
            mc.stdin.write(f'gc_impulse_probe {victim} 0\n'.encode());mc.stdin.flush();run.wait(.2);return observed()
        run.until(ready,'Actual native owner',seconds=90)
        run.action('motion');run.wait(.25);before=time.monotonic();run.wait(.1)
        run.until(lambda:actors(before),'Native starting position');start_actor=actors(before)[-1];report=command(1)
        run.until(lambda:impulses(),'Real wind impulse publication');run.wait(.4)
        sent=impulses();run.check('One impulse from one actual wind explosion',len(sent)==1 and report['events']==1)
        recipients=[q[3] for _,q in all_impulses()]
        run.check('Nearby native recipients each receive one independent impulse',len(recipients)==2 and len(set(recipients))==2)
        values=sent[0][1];delta=values[-3:]
        expected=(report['x']*640,-report['z']*640,report['y']*640)
        run.check('Final vanilla vector retained across the bridge',all(abs(a-b)<.01 for a,b in zip(delta,expected)))
        samples=actors(sent[0][0]);run.report.update(windSamples=samples,windStart=start_actor,windDelta=delta)
        run.check('Native velocity responds to wind',any(a['velocity'][0]<-1 and a['velocity'][2]>1 for a in samples))
        run.check('Real native movement integrates displacement',any(abs(a['origin'][0]-start_actor['origin'][0])>2 and a['origin'][2]>start_actor['origin'][2]+1 for a in samples))
        run.check('Damage-free wind preserves native health',all(a['health']==1000 for a in samples))
        run.report['windSamples']=samples;run.report['windDelta']=delta
        for mode in (2,3):
            run.action('motion');run.wait(.2);count=len(impulses());command(mode);run.wait(.2)
            run.check('Mod zero or cancellation suppresses native impulse '+str(mode),len(impulses())==count)
        run.report.update(passed=True,jarSha256=hashlib.sha256(args.jar.read_bytes()).hexdigest(),scope=__doc__)
    except BaseException as e:run.report.update(passed=False,error=str(e));raise
    finally:
        if mc:
            if mc.poll() is None:
                mc.stdin.write(b'stop\n');mc.stdin.flush()
                try:mc.wait(timeout=30)
                except subprocess.TimeoutExpired:mc.terminate();mc.wait(timeout=10)
            run.report.update(minecraftStopped=mc.poll() is not None,minecraftExitCode=mc.returncode)
        if relay:
            run.report['impulseFrames']=[dict(direction=d,type=k,payload=b.hex()) for _,d,k,b in relay.frames if k==101];relay.close()
        if log:log.close()
        logs=[x.read_text('utf-8',errors='replace') for x in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log'] if x.exists()]
        base.FIXTURE_LOG.write_text('\n'.join(logs).replace(run.config['csRconToken'],'[redacted]'),encoding='utf-8')
        run.close(out/'result.json')
    if not run.report['passed'] or not run.report['fixtureFilesRestored'] or not run.report['fixtureStopped'] or run.report['minecraftExitCode']:raise SystemExit(1)

if __name__=='__main__':main()
