"""Verify native death effects in a real isolated NeoForge server.

Build GoldCraft first. --candidate selects the built JAR; --jar selects a frozen
candidate. The default uses the
frozen pre-fix skill/life JAR for baseline comparisons. Profiles full/none check
optional-mod loading. All generated game data stays under build.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse, hashlib, json, os, shutil, subprocess, time

ROOT=Path(__file__).resolve().parent.parent
HERE=ROOT/'build/status-death-tests'
HERE.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--name',default='run-'+str(time.time_ns()));parser.add_argument('--candidate',action='store_true');parser.add_argument('--jar',type=Path);parser.add_argument('--expect-missing',action='store_true');parser.add_argument('--profile',choices=['full','none'],default='full');args=parser.parse_args()
if Path(args.name).name!=args.name:raise ValueError('Single directory name required')
WORK=HERE/args.name
if not WORK.resolve().is_relative_to(ROOT/'build') or any(p.is_symlink() or p.is_junction() for p in [WORK,*WORK.parents]):raise ValueError('Linked or external test output refused')
WORK.mkdir(exist_ok=False)
JAVA=ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe'
manifest=json.loads((ROOT/'build/neoforge-production/runServer.json').read_text('utf-8'))
libs=ROOT/'.tools/neoforge-runtime/21.1.256/libraries'
game=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-server.jar'
vanilla=libs/'net/minecraft/server/1.21.1-20240808.144430/server-1.21.1-20240808.144430-srg.jar'
universal=libs/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-universal.jar'
legacy=next(v.split('=',1)[1] for v in manifest['jvm'] if v.startswith('-DlegacyClassPath='))
epic=ROOT/'sandbox/neoforge-server/mods/epic-fight-21.17.3.1-mc1.21.1-neoforge.jar'
compile_jar=args.jar.resolve() if args.jar else ROOT/'neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar'
cp=';'.join(map(str,[game,vanilla,universal,legacy,epic,ROOT/'sandbox/neoforge-server/mods/NightFall-1.21.1-Neoforge-3.4.0.jar',compile_jar]))
def argfile(path,argv):path.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in argv),'utf-8')
classes=WORK/'classes';classes.mkdir()
probe=WORK/'StatusDeathProbe.java';shutil.copyfile(ROOT/'tests/status-death/StatusDeathProbe.java',probe)
probe_hash=hashlib.sha256(probe.read_bytes()).hexdigest()
shutil.copyfile(ROOT/'sandbox/cs-server/Half-Life/cstrike/maps/cs_assault.bsp',WORK/'cs_assault.bsp')
argfile(WORK/'javac.args',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',classes,probe])
r=subprocess.run([str(JAVA.with_name('javac.exe')),'@'+str(WORK/'javac.args')],cwd=WORK,capture_output=True)
(WORK/'compile.log').write_bytes(r.stdout+r.stderr)
if r.returncode:raise RuntimeError('Compile failed, see '+str(WORK/'compile.log'))
mods=WORK/'mods';mods.mkdir()
with ZipFile(mods/'status-death-probe.jar','w',ZIP_DEFLATED) as archive:
    archive.writestr('META-INF/neoforge.mods.toml','modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n[[mods]]\nmodId="goldcraft_status_death_probe"\nversion="1.0.0"\n')
    for file in classes.rglob('*.class'):archive.write(file,file.relative_to(classes).as_posix())
hashes={}
for name in ['goldcraft-neoforge-0.1.0-dev.jar','epic-fight-21.17.3.1-mc1.21.1-neoforge.jar','NightFall-1.21.1-Neoforge-3.4.0.jar','invincible-21.15.8.2-mc1.21.1-neoforge.jar','geckolib-neoforge-1.21.1-4.9.3.jar']:
    if args.profile=='none' and not name.startswith('goldcraft-'):continue
    source=(ROOT/'neoforge/build/libs'/name) if args.candidate and name.startswith('goldcraft-') else (ROOT/'build/candidates/combined-skill-guard-life-20261016'/name if name.startswith('goldcraft-') else ROOT/'sandbox/neoforge-server/mods'/name)
    if args.jar and name.startswith('goldcraft-'):source=args.jar.resolve()
    shutil.copyfile(source,mods/name);hashes[name]=hashlib.sha256(source.read_bytes()).hexdigest()
(WORK/'eula.txt').write_text('eula=true\n','ascii')
(WORK/'server.properties').write_text('server-ip=127.0.0.1\nserver-port=0\nonline-mode=true\nenable-rcon=false\nview-distance=2\nsimulation-distance=2\nlevel-type=minecraft:flat\ngenerate-structures=false\ngenerator-settings={"layers":[{"block":"minecraft:bedrock","height":1}],"biome":"minecraft:plains"}\n','ascii')
(WORK/'tmp').mkdir()
classpath=['-cp',';'.join(manifest['classpath'])] if manifest['classpath'] else []
argfile(WORK/'server.args',['-Xms256m','-Xmx1536m','-XX:ActiveProcessorCount=2',*[v for v in manifest['jvm'] if not v.startswith(('-Xmx','-Xms'))],f'-Djava.io.tmpdir={WORK/"tmp"}',*classpath,manifest['main'],*manifest['args']])
env={k:v for k,v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
for k in manifest.get('clearEnvironment',[]):env.pop(k,None)
env.update(manifest.get('environment',{}))
with (WORK/'runtime.log').open('wb') as log:
    p=subprocess.Popen([str(JAVA),'@'+str(WORK/'server.args')],cwd=WORK,env=env,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    print(json.dumps({'pid':p.pid,'fixture':str(WORK)}),flush=True)
    try:
        deadline=time.monotonic()+150
        while p.poll() is None and not (WORK/'status-death.json').exists():
            if time.monotonic()>deadline:raise TimeoutError('FML native death effects probe timeout')
            time.sleep(.25)
        if p.poll() is None:p.stdin.write(b'stop\n');p.stdin.flush()
        code=p.wait(timeout=30)
    except BaseException:
        p.terminate();p.wait(timeout=15);raise
report=json.loads((WORK/'status-death.json').read_text('utf-8'))
report.update(exitCode=code,modHashes=hashes,
              fixtureSourceSha256=probe_hash,
              boundary='Real NeoForge world, production proxy tick and vanilla removal effects. Host actor snapshots and player network connections are supplied by fixture; no live ReHLDS or graphical gameplay.')
if args.expect_missing:
    expected=not args.candidate and not args.jar and not report['passed'] and report.get('error')=='java.lang.AssertionError: native death invokes one wind explosion'
    report['expectedMissingReproduced']=expected
    passed=not code and expected
else:passed=not code and report['passed']
(HERE/(args.name+'-result.json')).write_text(json.dumps(report,indent=2)+'\n','utf-8')
print(json.dumps(report,indent=2),flush=True)
if not passed:raise SystemExit(1)
