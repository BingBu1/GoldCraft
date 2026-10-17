"""Validate depth conversion with a hidden real GL context and actual optional Mixin targets."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,zipfile
sys.stdout.reconfigure(encoding='utf-8')

R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,default=R/'build/depth-copy-tests')
p.add_argument('--mods',type=Path,required=True)
p.add_argument('--production-jar',type=Path)
a=p.parse_args();out=a.output.resolve()
if not out.is_relative_to(R/'build') or any(q.is_symlink() or q.is_junction() for q in (out,*out.parents)):raise ValueError('Use a plain workspace build directory')
out.mkdir(parents=True,exist_ok=True);classes=out/'classes';transform=out/'transform';classes.mkdir(exist_ok=True);transform.mkdir(exist_ok=True)
java=R/'.tools/java/jdk-21.0.12.1+1/bin';runtime=json.loads((R/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
game=list((R/'neoforge/.gradle/loom-cache/minecraftMaven/net/minecraft').glob('neoforge-21.1.256-minecraft-merged-*/1.21.1-loom.mappings.*/neoforge-21.1.256-*-neoforge-21.1.256.jar'))
assert len(game)==1;game=game[0]
aaa=list(a.mods.resolve().glob('aaa_particles-*.jar'));assert len(aaa)==1;aaa=aaa[0]
mixins=[R/'neoforge/src/client/java/dev/goldcraft/client/mixin'/n for n in ('FramebufferDepthMixin.java','DepthBlitMixin.java','AaaDepthBlitMixin.java')]
production=R/'neoforge/src/client/java/dev/goldcraft/client/DepthCopy.java'
sources=[production,R/'tests/depth-copy/DepthCopyProbe.java',R/'tests/depth-copy/ActualDepthCalls.java',R/'tests/depth-copy/DepthTransform.java',*mixins]
runs=[]
def run(name,exe,args):
    response=out/(name+'.args');response.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args),encoding='utf-8')
    result=subprocess.run([str(java/exe),'@'+str(response)],cwd=out,capture_output=True,timeout=120)
    (out/(name+'.log')).write_bytes(result.stdout+result.stderr);runs.append(dict(name=name,exitCode=result.returncode))
    print(name,result.returncode,(result.stdout+result.stderr).decode(errors='replace')[-1400:])
    if result.returncode:raise RuntimeError(name+' failed')
cp=os.pathsep.join(map(str,[classes,*runtime['classpath']]))
run('compile-gl','javac.exe',['--release','21','-proc:none','-cp',cp,'-d',classes,production,R/'tests/depth-copy/DepthCopyProbe.java',R/'tests/depth-copy/ActualDepthCalls.java'])
services=transform/'META-INF/services';services.mkdir(parents=True,exist_ok=True)
(services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
(transform/'depth.mixins.json').write_text(json.dumps(dict(required=True,package='dev.goldcraft.client.mixin',compatibilityLevel='JAVA_21',client=[s.stem for s in mixins],injectors=dict(defaultRequire=1))))
fixtures=[R/'tests/depth-copy/DepthTransform.java',*[R/'tests/nightfall-blade'/n for n in ('BladeMixinService.java','BladeProperties.java')]]
transform_cp=os.pathsep.join(map(str,[transform,classes,game,*runtime['classpath']]))
run('compile-transform','javac.exe',['--release','21','-proc:none','-cp',transform_cp,'-d',transform,*fixtures,*mixins])
run('transform-absent','java.exe',['-cp',transform_cp,'probe.DepthTransform',game,out,'development'])
run('transform-aaa','java.exe',['-cp',transform_cp+os.pathsep+str(aaa),'probe.DepthTransform',game,out,'development',aaa])
run('real-gl','java.exe',['-Dorg.lwjgl.system.SharedLibraryExtractPath='+str(out/'natives'),'-cp',cp,'DepthCopyProbe'])
if a.production_jar:
    jar=a.production_jar.resolve();prod=out/'production';prod.mkdir(exist_ok=True)
    with zipfile.ZipFile(jar) as archive:
        for mixin in mixins:
            name='dev/goldcraft/client/mixin/'+mixin.stem+'.class';target=prod/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(archive.read(name))
        for name in archive.namelist():
            if name.startswith('dev/goldcraft/client/DepthCopy') and name.endswith('.class'):
                target=prod/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(archive.read(name))
    games=list((R/'.tools/gradle-cache/caches/fabric-loom/minecraftMaven/net/minecraft/neoforge-21.1.256-minecraft-merged-mojang').rglob('*.jar'));assert len(games)==1
    prod_cp=os.pathsep.join(map(str,[prod,transform,classes,games[0],aaa,*runtime['classpath']]))
    run('production-transform','java.exe',['-cp',prod_cp,'probe.DepthTransform',games[0],out,'production',aaa])
    run('production-real-gl','java.exe',['-Dorg.lwjgl.system.SharedLibraryExtractPath='+str(out/'natives'),'-cp',str(prod)+os.pathsep+cp,'DepthCopyProbe'])
    sources.append(jar)
report=dict(passed=True,runs=runs,scope='Real GPU pixel/state checks and actual Sponge targets; no live game injection or gameplay/FPS acceptance',inputs={str(s.relative_to(R)):hashlib.sha256(s.read_bytes()).hexdigest() for s in [*sources,game,aaa]})
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS depth copy validation')
