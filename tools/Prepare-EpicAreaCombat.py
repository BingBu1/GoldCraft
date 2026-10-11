from pathlib import Path
from zipfile import ZipFile,ZIP_DEFLATED
import argparse,json,subprocess,shutil
parser=argparse.ArgumentParser(description='Prepare the independent real Epic/native combat probe with stopped test servers.')
parser.add_argument('--instance',choices=['headless-combat','epic-area-combat'],default='headless-combat')
args=parser.parse_args()
ROOT=Path(__file__).resolve().parent.parent;WORK=ROOT/'sandbox'/args.instance;OUT=ROOT/'build/epic-area-probe'
for path in [WORK,OUT]:
    if not path.resolve().is_relative_to(ROOT):raise ValueError('Test output outside workspace')
    if any(p.is_symlink() or p.is_junction() for p in [path,*path.parents] if p!=ROOT):raise ValueError('Linked test output refused')
probe="$r='"+str(WORK).replace("'","''")+"';[bool](Get-CimInstance Win32_Process | Where-Object { ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($r+'\\')) -or ($_.Name -eq 'java.exe' -and $_.CommandLine.Contains($r+'\\server.args')) })"
if subprocess.check_output(['pwsh','-NoProfile','-Command',probe],text=True).strip()!='False':raise RuntimeError('Stop the independent test servers before preparing them')
OUT.mkdir(parents=True,exist_ok=True)
if not (WORK/'cluster.json').is_file():raise RuntimeError('Run tools/Initialize-HeadlessCombat.ps1 first, with both independent test servers stopped.')
manifest=json.loads((ROOT/'build/neoforge-production/runServer.json').read_text('utf-8'))
JAVA=ROOT/'.tools/java/jdk-21.0.12.1+1/bin/java.exe'
libraries=ROOT/'.tools/neoforge-runtime/21.1.256/libraries'
game=libraries/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-server.jar'
vanilla=libraries/'net/minecraft/server/1.21.1-20240808.144430/server-1.21.1-20240808.144430-srg.jar'
universal=libraries/'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-universal.jar'
legacy=next(v.split('=',1)[1] for v in manifest['jvm'] if v.startswith('-DlegacyClassPath='))
epic=ROOT/'sandbox/neoforge-server/mods/epic-fight-21.17.3.1-mc1.21.1-neoforge.jar'
cp=';'.join(map(str,[game,vanilla,universal,legacy,ROOT/'neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar',epic]))
def argfile(path,args):path.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in args),'utf-8')
classes=OUT/'classes';classes.mkdir(exist_ok=True)
argfile(OUT/'compile.args',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',classes,ROOT/'tools/java/IntegratedAreaProbe.java'])
p=subprocess.run([str(JAVA.with_name('javac.exe')),'@'+str(OUT/'compile.args')],cwd=OUT,capture_output=True);(OUT/'compile.log').write_bytes(p.stdout+p.stderr)
if p.returncode:raise RuntimeError('Integrated probe compile failed')
mods=WORK/'minecraft/mods';mods.mkdir(exist_ok=True)
with ZipFile(mods/'area-probe.jar','w',ZIP_DEFLATED) as archive:
    archive.writestr('META-INF/neoforge.mods.toml','modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n[[mods]]\nmodId="goldcraft_area_probe"\nversion="1.0.0"\n')
    for file in classes.rglob('*.class'):archive.write(file,file.relative_to(classes).as_posix())
for name in ['epic-fight-21.17.3.1-mc1.21.1-neoforge.jar','goldcraft-neoforge-0.1.0-dev.jar','NightFall-1.21.1-Neoforge-3.4.0.jar','invincible-21.15.8.2-mc1.21.1-neoforge.jar','geckolib-neoforge-1.21.1-4.9.3.jar']:
    source=ROOT/'neoforge/build/libs'/name if name.startswith('goldcraft-') else ROOT/'sandbox/neoforge-server/mods'/name
    shutil.copyfile(source,mods/name)
classpath=['-cp',';'.join(manifest['classpath'])] if manifest['classpath'] else []
argfile(WORK/'server.args',['-Xms256m','-Xmx1536m',*[a for a in manifest['jvm'] if not a.startswith(('-Xms','-Xmx'))],*classpath,manifest['main'],*manifest['args']])
shutil.copyfile(ROOT/'build/regamedll/Release/mp.dll',WORK/'Half-Life/cstrike/dlls/mp.dll')
shutil.copyfile(ROOT/'build/amxx/plugins/goldcraft_headless_test.amxx',WORK/'Half-Life/cstrike/addons/amxmodx/plugins/goldcraft_headless_test.amxx')
print('Prepared isolated Epic/native combat fixture')
