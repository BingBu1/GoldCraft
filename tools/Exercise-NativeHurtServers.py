"""Connect real ReHLDS and NeoForge and observe settled hurt callbacks through a byte-preserving relay.

Only the player and effect grant are fixtures. No actor/hit/payment packet is
generated or changed by this harness. No graphical-client acceptance is claimed.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse
import hashlib
import importlib.util
import json
import os
import select
import shutil
import socket
import struct
import subprocess
import threading
import time

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('hurt_native',ROOT/'tools/Exercise-NativeHurt.py')
native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
base=native.base
HEADER=struct.Struct('<IHHIIQQ')


class Relay:
    def __init__(self,port):
        self.hostport=port;self.stop=False;self.error=None;self.frames=[];self.sockets=[]
        self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(1);self.listener.settimeout(.2)
        self.port=self.listener.getsockname()[1];self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
    def run(self):
        try:
            while not self.stop:
                try:mc,_=self.listener.accept();break
                except socket.timeout:continue
            if self.stop:return
            host=socket.create_connection(('127.0.0.1',self.hostport));self.sockets=[host,mc]
            buffers={host:b'',mc:b''};other={host:mc,mc:host}
            while not self.stop:
                for s in select.select(self.sockets,[],[],.1)[0]:
                    data=s.recv(65536)
                    if not data:return
                    other[s].sendall(data);buffers[s]+=data
                    while len(buffers[s])>=HEADER.size:
                        magic,version,kind,size,flags,epoch,sequence=HEADER.unpack_from(buffers[s])
                        if magic!=0x31464347 or flags or size>32*1024*1024:raise ValueError('Bad production frame')
                        if len(buffers[s])<HEADER.size+size:break
                        body=buffers[s][HEADER.size:HEADER.size+size];buffers[s]=buffers[s][HEADER.size+size:]
                        if kind in (100,77,78):self.frames.append((time.monotonic(),'host' if s is host else 'mc',kind,body))
        except BaseException as e:
            if not self.stop:self.error=str(e)
    def close(self):
        self.stop=True;self.thread.join(2)
        for s in self.sockets:s.close()
        self.listener.close()


def prepare_mc(work,jar,port,config):
    java=ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe'
    manifest=json.loads((ROOT/'build/neoforge-production/runServer.json').read_text(encoding='utf-8'))
    libs=ROOT/'.tools/neoforge-runtime/21.1.256/libraries'
    dependencies=[libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-server.jar',libs/'net/minecraft/server/1.21.1-20240808.144430/server-1.21.1-20240808.144430-srg.jar',libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-universal.jar']
    legacy=next(v.split('=',1)[1] for v in manifest['jvm'] if v.startswith('-DlegacyClassPath='))
    work.mkdir();classes=work/'classes';classes.mkdir();mods=work/'mods';mods.mkdir();(work/'tmp').mkdir()
    source=ROOT/'tests/status-hit/NativeHurtServersProbe.java';shutil.copyfile(source,work/source.name)
    compile=subprocess.run([str(java.with_name('javac.exe')),'--release','21','-proc:none','-encoding','UTF-8','-cp',';'.join(map(str,[*dependencies,legacy,jar])),'-d',str(classes),str(work/source.name)],capture_output=True)
    (work/'compile.log').write_bytes(compile.stdout+compile.stderr)
    if compile.returncode:raise RuntimeError('Conduit integration probe compile failed')
    with ZipFile(mods/'native-hurt-servers-probe.jar','w',ZIP_DEFLATED) as z:
        z.writestr('META-INF/neoforge.mods.toml','modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n[[mods]]\nmodId="goldcraft_native_hurt_servers_probe"\nversion="1.0.0"\n')
        for p in classes.rglob('*.class'):z.write(p,p.relative_to(classes).as_posix())
    shutil.copyfile(jar,mods/jar.name)
    (work/'eula.txt').write_text('eula=true\n')
    settings=dict(biome='minecraft:plains',features=False,lakes=False,layers=[dict(height=1,block='minecraft:air')],structure_overrides=[])
    (work/'server.properties').write_text('server-ip=127.0.0.1\nserver-port=0\nonline-mode=true\nenable-rcon=false\nview-distance=2\nsimulation-distance=2\nlevel-type=minecraft:flat\ngenerate-structures=false\ngenerator-settings='+json.dumps(settings)+'\n')
    pack=work/'world/datapacks/goldcraft_conduit';dimensions=pack/'data/goldcraft/dimension';dimensions.mkdir(parents=True)
    (pack/'pack.mcmeta').write_text(json.dumps(dict(pack=dict(pack_format=48,description='Conduit real servers fixture'))))
    (dimensions/'cs_assault_f6725c06.json').write_text(json.dumps(dict(type='minecraft:overworld',generator=dict(type='minecraft:flat',settings=settings))))
    runtime_cp=['-cp',';'.join(manifest['classpath'])] if manifest['classpath'] else []
    args=['-Xms256m','-Xmx1536m','-XX:ActiveProcessorCount=2',*[v for v in manifest['jvm'] if not v.startswith(('-Xmx','-Xms'))],f'-Djava.io.tmpdir={work/"tmp"}',*runtime_cp,manifest['main'],*manifest['args']]
    argfile=work/'server.args';argfile.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args),'utf-8')
    env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
    for key in manifest.get('clearEnvironment',[]):env.pop(key,None)
    env.update(manifest.get('environment',{}));env['GOLDCRAFT_SERVER_PORT']=str(port)
    for key in ['SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=config['server'+key.title()]
    return java,argfile,env


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--name',required=True);parser.add_argument('--jar',type=Path,required=True);args=parser.parse_args()
    if Path(args.name).name!=args.name:raise ValueError('Single output name required')
    out=ROOT/'build/native-hit-notify-20261018'/args.name
    if any(p.is_symlink() or p.is_junction() for p in [out,*out.parents]):raise ValueError('Linked output refused')
    out.mkdir(exist_ok=False);base.FIXTURE_LOG=out/'server.log';base.FIXTURE_LOG.write_text('')
    run=native.Run(out);run.report['scope']=__doc__;relay=mc=log=None
    def observed():
        path=out/'minecraft/native-hurt-live.json'
        try:return json.loads(path.read_text()) if path.exists() else None
        except json.JSONDecodeError:return None
    def count():
        r=observed();return r['callbacks'] if r else -1
    def effect(mode):
        mc.stdin.write(('gc_hurt_probe '+str(victim)+' '+str(mode)+'\n').encode());mc.stdin.flush()
    def hits():return [struct.unpack('<3Q8I4f',b) for _,d,k,b in relay.frames if d=='host' and k==100]
    try:
        run.prepare();env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
        for key in ['PORT','SESSION','TOKEN']:env['GOLDCRAFT_SERVER_'+key]=str(run.config['server'+key.title()])
        env.update(GOLDCRAFT_HTTP_ENABLED='0',GOLDCRAFT_SERVER_LOG=str(out/'bridge.log'))
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=subprocess.SW_HIDE
        run.process=subprocess.Popen([str(base.GAME/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+sv_rcon_condebug','0','+ip','127.0.0.1','-port',str(run.config['csPort']),'-maxplayers','4','+map','cs_assault'],cwd=base.GAME,env=env,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
        run.report['pid']=run.process.pid;run.ready();relay=Relay(run.config['serverPort'])
        java,argfile,env=prepare_mc(out/'minecraft',args.jar.resolve(),relay.port,run.config)
        log=(out/'minecraft/runtime.log').open('wb')
        mc=subprocess.Popen([str(java),'@'+str(argfile)],cwd=out/'minecraft',env=env,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        run.report['minecraftPid']=mc.pid;print(json.dumps(dict(nativePid=run.process.pid,minecraftPid=mc.pid)),flush=True)
        created=run.action('create');victim=int(created['victim']);run.command('sv_restart 1');run.wait(1.5)
        def ready():
            effect(1);run.wait(.2);return observed()
        run.until(ready,'Real MC effect owner',seconds=90)
        initial=count();run.action('attack 0 0');run.until(lambda:count()==initial+1,'Real knife callback')
        run.check('Real knife to native notification to original MC callback',observed()['amount']>0)
        before=count();run.action('attack 3 0');run.wait(.2)
        run.check('Real ReAPI cancellation causes no MC callback',count()==before)
        for kind in (2,3,4,5,6,7):
            before=count();run.action(f'hurt {kind} 0');run.until(lambda:count()==before+1,'Real source callback '+str(kind))
            run.check('Real source '+str(kind)+' callback once',abs(observed()['amount']-5)<.001)
        before=count();run.action('hurt 8 0');run.until(lambda:count()==before+1,'Lethal callback before retirement')
        run.check('Real fatal native hit delivers before effect retirement',True)
        run.action('hurt 1 0');run.wait(.25);before=count();effect(2)
        run.until(lambda:observed() and observed().get('mode')==2,'Absorption grant command')
        run.report['absorptionGrant']=observed()
        def absorption_sent():
            return any(d=='mc' and k==77 and struct.unpack_from('<I',b,32)[0]==victim
                and struct.unpack_from('<f',b,60)[0]>=5 for _,d,k,b in relay.frames)
        run.until(absorption_sent,'Real positive absorption publication');run.wait(.1)
        state=run.action('hurt 1 0');run.until(lambda:count()==before+1,'Real absorption settlement')
        run.check('Real MC absorption payment triggers one callback',state['health']==1000)
        run.wait(.25);run.check('No duplicate callback after health snapshots',count()==before+1)
        run.report['nativeHitFrames']=len(hits());run.report['callbacks']=observed()
        run.report['jarSha256']=hashlib.sha256(args.jar.read_bytes()).hexdigest();run.report['passed']=True
    except BaseException as e:run.report.update(passed=False,error=str(e));raise
    finally:
        if mc:
            if mc.poll() is None:
                mc.stdin.write(b'stop\n');mc.stdin.flush()
                try:mc.wait(timeout=30)
                except subprocess.TimeoutExpired:mc.terminate();mc.wait(timeout=10)
            run.report['minecraftStopped']=mc.poll() is not None;run.report['minecraftExitCode']=mc.returncode
        if relay:
            run.report['frames']=[dict(direction=d,type=k,payload=b.hex()) for _,d,k,b in relay.frames]
            relay.close()
        if log:log.close()
        logs=[p.read_text('utf-8',errors='replace') for p in [base.GAME/'qconsole.log',base.GAME/'cstrike/qconsole.log'] if p.exists()]
        base.FIXTURE_LOG.write_text('\n'.join(logs).replace(run.config['csRconToken'],'[redacted]'),encoding='utf-8')
        run.close(out/'result.json')
    if not run.report['passed'] or not run.report['fixtureFilesRestored'] or not run.report['fixtureStopped'] or run.report['minecraftExitCode']:raise SystemExit(1)


if __name__=='__main__':main()
