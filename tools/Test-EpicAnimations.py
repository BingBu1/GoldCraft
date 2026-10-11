"""Diagnose actual Epic/NightFall registered clips in an isolated NeoForge server.

Uses deployed Mods unless --candidate selects the built core. Missing registered
resources make the report fail; this does not suppress or replace animations.
No production runtime is changed. Do not run during live FPS measurements.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse, hashlib, json, os, re, shutil, subprocess, time

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'sources.lock.json').is_file())
HERE=ROOT/'analysis/epic-animation-tests'
HERE.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--name',default='run-'+str(time.time_ns()));parser.add_argument('--candidate',action='store_true');parser.add_argument('--warning-log',type=Path,default=ROOT/'sandbox/neoforge-cs-client-b/logs/latest.log');args=parser.parse_args()
if Path(args.name).name!=args.name:raise ValueError('Single directory name required')
WORK=HERE/args.name
if not WORK.resolve().is_relative_to(ROOT/'analysis') or any(p.is_symlink() or p.is_junction() for p in [WORK,*WORK.parents]):raise ValueError('Linked or external test output refused')
WORK.mkdir(exist_ok=False)
JAVA=ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe'
manifest=json.loads((ROOT/'build/neoforge-production/runServer.json').read_text('utf-8'))
libs=ROOT/'.tools/neoforge-runtime/21.1.256/libraries'
game=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-server.jar'
vanilla=libs/'net/minecraft/server/1.21.1-20240808.144430/server-1.21.1-20240808.144430-srg.jar'
universal=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-universal.jar'
legacy=next(v.split('=',1)[1] for v in manifest['jvm'] if v.startswith('-DlegacyClassPath='))
epic=ROOT/'sandbox/neoforge-server/mods/epic-fight-21.17.3.1-mc1.21.1-neoforge.jar'
cp=';'.join(map(str,[game,vanilla,universal,legacy,epic]))
def argfile(path,argv):path.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in argv),'utf-8')
classes=WORK/'classes';classes.mkdir()
argfile(WORK/'javac.args',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',classes,Path(__file__).resolve().parent/'java/AnimationRegistryProbe.java'])
r=subprocess.run([str(JAVA.with_name('javac.exe')),'@'+str(WORK/'javac.args')],cwd=WORK,capture_output=True)
(WORK/'compile.log').write_bytes(r.stdout+r.stderr)
if r.returncode:raise RuntimeError('Compile failed, see '+str(WORK/'compile.log'))
warning_text=args.warning_log.read_text('utf-8',errors='replace') if args.warning_log.is_file() else ''
warning_ids=sorted(set(re.findall(r'Datapack animation reading failed: No constructor information has provided: ([a-z0-9_:/.-]+)',warning_text)))
(WORK/'reported-animation-ids.json').write_text(json.dumps(warning_ids),'utf-8')
mods=WORK/'mods';mods.mkdir()
with ZipFile(mods/'animation-probe.jar','w',ZIP_DEFLATED) as archive:
    archive.writestr('META-INF/neoforge.mods.toml','modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n[[mods]]\nmodId="goldcraft_animation_probe"\nversion="1.0.0"\n')
    for file in classes.rglob('*.class'):archive.write(file,file.relative_to(classes).as_posix())
hashes={}
for name in ['goldcraft-neoforge-0.1.0-dev.jar','epic-fight-21.17.3.1-mc1.21.1-neoforge.jar','NightFall-1.21.1-Neoforge-3.4.0.jar','invincible-21.15.8.2-mc1.21.1-neoforge.jar','geckolib-neoforge-1.21.1-4.9.3.jar']:
    source=(ROOT/'neoforge/build/libs'/name) if args.candidate and name.startswith('goldcraft-') else ROOT/'sandbox/neoforge-server/mods'/name
    shutil.copyfile(source,mods/name);hashes[name]=hashlib.sha256(source.read_bytes()).hexdigest()
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
        while p.poll() is None and not (WORK/'animation-registry.json').exists():
            if time.monotonic()>deadline:raise TimeoutError('FML probe timeout')
            time.sleep(.25)
        if p.poll() is None:p.stdin.write(b'stop\n');p.stdin.flush()
        code=p.wait(timeout=30)
    except BaseException:
        p.terminate();p.wait(timeout=15);raise
report=json.loads((WORK/'animation-registry.json').read_text('utf-8'))
report.update(exitCode=code,modHashes=hashes)
(HERE/(args.name+'-result.json')).write_text(json.dumps(report,indent=2)+'\n','utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('animations','reportedIdsRegistered','warnings')},indent=2),flush=True)
print(json.dumps({'checked':len(report['animations']),'reportedIdsRegistered':sum(report.get('reportedIdsRegistered',{}).values()),'emptyClips':len(report.get('warnings',[]))}))
if code or not report['passed']:raise SystemExit(1)
