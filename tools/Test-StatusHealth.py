"""Run real NeoForge health, player attributes, effects and death-loot probes."""
from pathlib import Path
from zipfile import ZipFile,ZIP_DEFLATED
import argparse,hashlib,json,os,shutil,subprocess,time

ROOT=Path(__file__).resolve().parent.parent
out=ROOT/'build/status-health-mc-20261017'
parser=argparse.ArgumentParser()
parser.add_argument('--name',default='fml-attribute-'+str(time.time_ns()))
parser.add_argument('--jar',type=Path,default=ROOT/'neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar')
parser.add_argument('--profile',choices=['none','full'],default='none')
args=parser.parse_args()
if Path(args.name).name!=args.name:raise ValueError('Single output name required')
work=out/args.name
if not work.resolve().is_relative_to(ROOT/'build') or any(p.is_symlink() or p.is_junction() for p in [work,*work.parents]):
    raise ValueError('Linked or external fixture output refused')
if work.exists():raise FileExistsError(work)
work.mkdir(parents=True)
java=ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe'
manifest=json.loads((ROOT/'build/neoforge-production/runServer.json').read_text(encoding='utf-8'))
libs=ROOT/'.tools/neoforge-runtime/21.1.256/libraries'
game=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-server.jar'
vanilla=libs/'net/minecraft/server/1.21.1-20240808.144430/server-1.21.1-20240808.144430-srg.jar'
universal=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-universal.jar'
mods=work/'mods';mods.mkdir()
goldcraft=args.jar.resolve()
legacy=next(v.split('=',1)[1] for v in manifest['jvm'] if v.startswith('-DlegacyClassPath='))
classpath=';'.join(map(str,[game,vanilla,universal,legacy,goldcraft]))
probes=sorted((ROOT/'tests/status-health').glob('*.java'))
for source in probes:shutil.copyfile(source,work/source.name)
probe=work/'NativeHealthProbe.java'
classes=work/'classes';classes.mkdir()
compileResult=subprocess.run([str(java.with_name('javac.exe')),'--release','21','-proc:none','-encoding','UTF-8','-cp',classpath,'-d',str(classes),*[str(work/p.name) for p in probes]],capture_output=True)
(work/'compile.log').write_bytes(compileResult.stdout+compileResult.stderr)
if compileResult.returncode:raise RuntimeError('Compile failed; see '+str(work/'compile.log'))
with ZipFile(mods/'native-health-probe.jar','w',ZIP_DEFLATED) as jar:
    jar.writestr('META-INF/neoforge.mods.toml','modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n[[mods]]\nmodId="goldcraft_native_health_probe"\nversion="1.0.0"\n')
    # Two zero-weight entries expose the real death loot context's luck sign:
    # positive selects gold, negative selects diamond, zero produces no loot.
    jar.writestr('data/minecraft/loot_table/entities/cow.json',json.dumps({'type':'minecraft:entity','pools':[{'rolls':1,'entries':[
        {'type':'minecraft:item','name':'minecraft:gold_ingot','weight':0,'quality':1},
        {'type':'minecraft:item','name':'minecraft:diamond','weight':0,'quality':-1}]}]}))
    for file in classes.rglob('*.class'):jar.write(file,file.relative_to(classes).as_posix())
shutil.copyfile(goldcraft,mods/goldcraft.name)
mod_hashes={goldcraft.name:hashlib.sha256(goldcraft.read_bytes()).hexdigest()}
if args.profile=='full':
    for name in ['epic-fight-21.17.3.1-mc1.21.1-neoforge.jar','NightFall-1.21.1-Neoforge-3.4.0.jar',
                 'invincible-21.15.8.2-mc1.21.1-neoforge.jar','geckolib-neoforge-1.21.1-4.9.3.jar']:
        source=ROOT/'sandbox/neoforge-server/mods'/name
        shutil.copyfile(source,mods/name);mod_hashes[name]=hashlib.sha256(source.read_bytes()).hexdigest()
profile=args.profile
probe_hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in probes}
shutil.copyfile(ROOT/'sandbox/cs-server/Half-Life/cstrike/maps/cs_assault.bsp',work/'cs_assault.bsp')
(work/'eula.txt').write_text('eula=true\n','ascii')
(work/'server.properties').write_text('server-ip=127.0.0.1\nserver-port=0\nonline-mode=true\nenable-rcon=false\nview-distance=2\nsimulation-distance=2\nlevel-type=minecraft:flat\ngenerate-structures=false\ngenerator-settings={"layers":[{"block":"minecraft:bedrock","height":1}],"biome":"minecraft:plains"}\n','ascii')
(work/'tmp').mkdir()
argfile=work/'server.args'
runtime_classpath=['-cp',';'.join(manifest['classpath'])] if manifest['classpath'] else []
args=['-Xms256m','-Xmx1536m','-XX:ActiveProcessorCount=2',*[v for v in manifest['jvm'] if not v.startswith(('-Xmx','-Xms'))],f'-Djava.io.tmpdir={work/"tmp"}',*runtime_classpath,manifest['main'],*manifest['args']]
argfile.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args),'utf-8')
env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
for key in manifest.get('clearEnvironment',[]):env.pop(key,None)
env.update(manifest.get('environment',{}))
start=subprocess.STARTUPINFO();start.dwFlags|=subprocess.STARTF_USESHOWWINDOW;start.wShowWindow=subprocess.SW_HIDE
with (work/'runtime.log').open('wb') as log:
    p=subprocess.Popen([str(java),'@'+str(argfile)],cwd=work,env=env,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,startupinfo=start,creationflags=subprocess.CREATE_NO_WINDOW)
    (work/'process.json').write_text(json.dumps(dict(pid=p.pid,startedUtc=time.time(),modHashes=mod_hashes,fixtureSourceHashes=probe_hashes),indent=2)+'\n')
    print(json.dumps(dict(pid=p.pid,fixture=str(work))),flush=True)
    try:
        deadline=time.monotonic()+150
        while p.poll() is None and not(work/'native-health.json').exists():
            if time.monotonic()>deadline:raise TimeoutError('NeoForge health probe did not report')
            time.sleep(.25)
        report=json.loads((work/'native-health.json').read_text(encoding='utf-8'))
        if p.poll() is None:p.stdin.write(b'stop\n');p.stdin.flush()
        code=p.wait(timeout=30)
    except BaseException:
        p.terminate();p.wait(timeout=15);raise
report.update(exitCode=code,profile=profile,modHashes=mod_hashes,fixtureSourceHashes=probe_hashes,
              boundary='Real NeoForge entities, effects, lifecycle and BridgeLink TCP publisher. Host snapshots, acknowledgements and client connection are fixtures, not ReHLDS or multiplayer gameplay.')
(work/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
if not report.get('passed') or code:raise SystemExit(1)
