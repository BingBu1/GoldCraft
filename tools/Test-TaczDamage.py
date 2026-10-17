"""Run actual NeoForge/TACZ projectile callbacks in an isolated loopback server.

Build the production JAR first. Only a fixture hurt sink replaces native network
delivery; Mod registries, bytecode transformation and the hurt pipeline are real.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse, hashlib, json, os, shutil, subprocess, time

ROOT=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--name',default='run-'+str(time.time_ns()))
parser.add_argument('--without-tacz',action='store_true')
parser.add_argument('--jar',type=Path,default=ROOT/'neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar')
args=parser.parse_args()
args.jar=args.jar.resolve()
if Path(args.name).name!=args.name:raise ValueError('Single output directory name required')
WORK=ROOT/'build/projectile-damage-tests'/args.name
if any(p.is_symlink() or p.is_junction() for p in [WORK,*WORK.parents]):raise ValueError('Linked output refused')
WORK.mkdir(parents=True,exist_ok=False)
JAVA=ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe'
manifest=json.loads((ROOT/'build/neoforge-production/runServer.json').read_text('utf-8'))
libs=ROOT/'.tools/neoforge-runtime/21.1.256/libraries'
game=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-server.jar'
vanilla=libs/'net/minecraft/server/1.21.1-20240808.144430/server-1.21.1-20240808.144430-srg.jar'
universal=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-universal.jar'
legacy=next(v.split('=',1)[1] for v in manifest['jvm'] if v.startswith('-DlegacyClassPath='))
tacz=ROOT/'sandbox/modpack-neoforge/GoldCraft-1.21.1-NeoForge/mods/tacz-neoforge-1.21.1-1.1.8-hotfix-r7.jar'
cp=';'.join(map(str,[game,vanilla,universal,legacy,args.jar,tacz]))
def argfile(path,argv):path.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in argv),'utf-8')
classes=WORK/'classes';classes.mkdir()
probe=ROOT/'tests/projectile-damage'/('OptionalProjectileProbe.java' if args.without_tacz else 'TaczDamageProbe.java')
argfile(WORK/'javac.args',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',classes,probe])
r=subprocess.run([str(JAVA.with_name('javac.exe')),'@'+str(WORK/'javac.args')],cwd=WORK,capture_output=True)
(WORK/'compile.log').write_bytes(r.stdout+r.stderr)
if r.returncode:raise RuntimeError('Compile failed, see '+str(WORK/'compile.log'))
mods=WORK/'mods';mods.mkdir()
with ZipFile(mods/'projectile-probe.jar','w',ZIP_DEFLATED) as archive:
    archive.writestr('META-INF/neoforge.mods.toml','modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n[[mods]]\nmodId="goldcraft_projectile_probe"\nversion="1.0.0"\n')
    for file in classes.rglob('*.class'):archive.write(file,file.relative_to(classes).as_posix())
hashes={}
for source in [args.jar]+([] if args.without_tacz else [tacz]):
    shutil.copyfile(source,mods/source.name);hashes[source.name]=hashlib.sha256(source.read_bytes()).hexdigest()
shutil.copyfile(ROOT/'sandbox/cs-server/Half-Life/cstrike/maps/cs_assault.bsp',WORK/'cs_assault.bsp')
(WORK/'eula.txt').write_text('eula=true\n','ascii')
(WORK/'server.properties').write_text('server-ip=127.0.0.1\nserver-port=0\nonline-mode=true\nenable-rcon=false\nview-distance=2\nsimulation-distance=2\nlevel-type=minecraft:flat\ngenerate-structures=false\ngenerator-settings={"layers":[{"block":"minecraft:bedrock","height":1}],"biome":"minecraft:plains"}\n','ascii')
(WORK/'tmp').mkdir()
classpath=['-cp',';'.join(manifest['classpath'])] if manifest['classpath'] else []
argfile(WORK/'server.args',['-Xms256m','-Xmx1536m',*[v for v in manifest['jvm'] if not v.startswith(('-Xmx','-Xms'))],f'-Djava.io.tmpdir={WORK/"tmp"}',*classpath,manifest['main'],*manifest['args']])
env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
for k in manifest.get('clearEnvironment',[]):env.pop(k,None)
env.update(manifest.get('environment',{}))
with (WORK/'runtime.log').open('wb') as log:
    p=subprocess.Popen([str(JAVA),'@'+str(WORK/'server.args')],cwd=WORK,env=env,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    print(json.dumps({'pid':p.pid,'fixture':str(WORK)}),flush=True)
    try:
        deadline=time.monotonic()+150
        while p.poll() is None and not (WORK/'projectile-result.json').exists():
            if time.monotonic()>deadline:raise TimeoutError('FML projectile probe timeout')
            time.sleep(.25)
        if p.poll() is None:p.stdin.write(b'stop\n');p.stdin.flush()
        code=p.wait(timeout=30)
    except BaseException:
        p.terminate();p.wait(timeout=15);raise
result=json.loads((WORK/'projectile-result.json').read_text()) if (WORK/'projectile-result.json').exists() else {'passed':False,'error':'No probe result'}
result.update(exitCode=code,jarHashes=hashes,probeSha256=hashlib.sha256(probe.read_bytes()).hexdigest(),withoutTacz=args.without_tacz,
              boundary='Real NeoForge/TACZ transform, registries and hurt pipeline; host actors and native damage sink are fixtures, not multiplayer/live native receipts.')
(WORK/'result.json').write_text(json.dumps(result,indent=2),'utf-8')
print(json.dumps(result),flush=True)
raise SystemExit(0 if code==0 and result.get('passed') else 1)
