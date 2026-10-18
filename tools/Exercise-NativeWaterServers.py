"""Verify real MC water/effects reach the native PM movement path over both servers.

Fake players, effect grants and single engine movement commands are fixtures.
No fluid/actor/water packet is generated or rewritten by the byte relay.
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
    out=ROOT/'build/native-water-20261018'/args.name
    if any(x.is_symlink() or x.is_junction() for x in [out,*out.parents]):raise ValueError('Linked output refused')
    out.mkdir(exist_ok=False);base.FIXTURE_LOG=out/'server.log';base.FIXTURE_LOG.write_text('')
    run=dual.native.Run(out,args.server_dll);run.report['scope']=__doc__;mc=relay=log=None
    def observed():
        try:return json.loads((out/'minecraft/native-water-live.json').read_text())
        except (FileNotFoundError,json.JSONDecodeError):return None
    def command(mode):
        before=observed();mc.stdin.write(f'gc_water_probe {victim} {mode}\n'.encode());mc.stdin.flush()
        run.until(lambda:observed() and observed()['mode']==mode and (not before or observed()['calls']>before['calls']),'MC water command')
    def states():
        return [(t,struct.unpack('<2Q5I5f',b)) for t,d,k,b in relay.frames if d=='mc' and k==102 and struct.unpack_from('<I',b,16)[0]==victim]
    def latest():
        rows=states();return rows[-1][1] if rows else None
    try:
        run.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(run.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(out/'bridge.log'))
        start=subprocess.STARTUPINFO();start.dwFlags|=subprocess.STARTF_USESHOWWINDOW;start.wShowWindow=subprocess.SW_HIDE
        run.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(run.config['csPort']),'-maxplayers','4','+map','cs_assault'],cwd=base.GAME,env=env,startupinfo=start,creationflags=subprocess.CREATE_NEW_CONSOLE)
        run.report['pid']=run.process.pid;run.ready()
        run.report['serverDllSha256']=hashlib.sha256((base.GAME/'cstrike/dlls/mp.dll').read_bytes()).hexdigest()
        run.check('Requested native candidate loaded',run.report['serverDllSha256']==hashlib.sha256(args.server_dll.read_bytes()).hexdigest())
        relay=dual.Relay(run.config['serverPort'],(102,))
        java,argfile,env=dual.prepare_mc(out/'minecraft',args.jar.resolve(),relay.port,run.config,ROOT/'tests/native-water/NativeWaterServersProbe.java','goldcraft_native_water_servers_probe')
        log=(out/'minecraft/runtime.log').open('wb');mc=subprocess.Popen([str(java),'@'+str(argfile)],cwd=out/'minecraft',env=env,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        run.report['minecraftPid']=mc.pid;print(json.dumps(dict(nativePid=run.process.pid,minecraftPid=mc.pid)),flush=True)
        victim=int(run.action('create')['victim']);run.command('sv_restart 1');run.wait(1.5)
        run.until(latest,'Real MC native water publisher',seconds=90)
        run.check('Native proxy keeps normal gravity despite no-gravity flag',abs(latest()[11]-.08)<.000001)
        command(1);run.until(lambda:latest()[6]==3,'Dolphin state');run.wait(.15)
        def submerged():
            value=run.action('waterstep');return value if value['water']==3 and value['kind']==-3 else None
        dolphin=run.until(submerged,'Real MC water reaches native PM',seconds=20)
        run.check('Real water movement uses distinct horizontal and vertical drag',abs(dolphin['vx']-108.288)<.02 and abs(dolphin['vz']-76.8)<.02)
        run.check('Real native hull movement advances position',dolphin['x']>524 and dolphin['z']>100)
        command(2);run.until(lambda:latest()[9]==1 and latest()[10]==2,'Actual water efficiency and swim attributes');run.wait(.1)
        attribute=run.action('waterstep')
        # Airborne efficiency is halved; it changes both acceleration and drag.
        expected=(100+(.02+(.1-.02)*.5)*2*640)*(.8+(.54600006-.8)*.5)
        run.check('Real attribute publication changes native acceleration',abs(attribute['vx']-expected)<.02 and attribute['x']>dolphin['x'])
        command(0);run.until(lambda:latest()[6]==0,'Effect and attributes removed');run.wait(.1);ordinary=run.action('waterstep')
        run.check('Ordinary CS water behavior restored',abs(ordinary['vx']-dolphin['vx'])>10 and abs(ordinary['vz']-80)<.02)
        command(3);run.until(lambda:latest()[6]==3,'Temporary effect');run.until(lambda:latest()[6]==0,'Real effect tick expiry',seconds=5)
        run.check('Real expiry clears native water override',True)
        command(1);run.until(lambda:latest()[6]==3,'Effect reapplied');life=latest()[5]
        run.action('hurt 8 0');run.wait(.2);run.action('motion');run.until(lambda:latest()[5]!=life,'Native respawn publication')
        run.check('New native life does not inherit old water effects',latest()[6]==0)
        run.report.update(passed=True,jarSha256=hashlib.sha256(args.jar.read_bytes()).hexdigest(),dolphin=dolphin,attributes=attribute,ordinary=ordinary)
    except BaseException as e:run.report.update(passed=False,error=str(e));raise
    finally:
        if mc:
            if mc.poll() is None:
                mc.stdin.write(b'stop\n');mc.stdin.flush()
                try:mc.wait(timeout=30)
                except subprocess.TimeoutExpired:mc.terminate();mc.wait(timeout=10)
            run.report.update(minecraftStopped=mc.poll() is not None,minecraftExitCode=mc.returncode)
        if relay:
            run.report['waterFrames']=len(states());relay.close()
        if log:log.close()
        logs=[x.read_text('utf-8',errors='replace') for x in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log'] if x.exists()]
        base.FIXTURE_LOG.write_text('\n'.join(logs).replace(run.config['csRconToken'],'[redacted]'),encoding='utf-8')
        run.close(out/'result.json')
    if not run.report['passed'] or not run.report['fixtureFilesRestored'] or not run.report['fixtureStopped'] or run.report['minecraftExitCode']:raise SystemExit(1)

if __name__=='__main__':main()
