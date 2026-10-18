"""Test actual distortion exporter and real Sponge accessor targets without starting games."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,zipfile

R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--production-jar',type=Path,required=True)
p.add_argument('--source-root',type=Path,default=R)
a=p.parse_args();out=a.output.resolve();source=a.source_root.resolve()
if not out.is_relative_to(R/'build') or any(q.is_symlink() or q.is_junction() for q in (out,*out.parents)):raise ValueError('Use a plain workspace build directory')
src=out/'src';classes=out/'classes';transform=out/'transform'
for d in (src,classes,transform):d.mkdir(parents=True,exist_ok=True)
adapters={
'dev/goldcraft/client/GoldCraftClient.java':'''public class GoldCraftClient {public static final Host HOST=new Host();public static class Host {public boolean present=true;public long epoch(){return 99;}public Actor actor(java.util.UUID id){return present?new Actor():null;}}public static class Actor{public int serial(){return 7;}public int life(){return 3;}}}''',
'dev/goldcraft/client/HostInput.java':'''public class HostInput {public static boolean hosted=true;public static int life=3;public static boolean hosted(){return hosted;}public static int life(){return life;}}''',
'dev/goldcraft/client/HudExporter.java':'''public class HudExporter {public static boolean dead;public static boolean nativeDeathHud(net.minecraft.client.MinecraftClient c){return dead;}}''',
'dev/goldcraft/bridge/BridgeLink.java':'''public class BridgeLink {public boolean enabled=true;public int sent;public byte[] last;public long generation(){return 4;}public boolean viewDistortion(long g){return enabled&&g==4;}public boolean sendViewDistortion(long g,byte[] p){if(!viewDistortion(g))throw new AssertionError();sent++;last=p;return true;}}''',
'net/minecraft/client/MinecraftClient.java':'''public class MinecraftClient {public Player player=new Player();public Options options=new Options();public net.minecraft.client.render.GameRenderer gameRenderer=new net.minecraft.client.render.GameRenderer();public float delta;
public MinecraftClient getRenderTickCounter(){return this;}public float getTickDelta(boolean paused){return delta;}
public static class Player {public float prevNauseaIntensity,nauseaIntensity;public boolean alive=true,nausea;public boolean isAlive(){return alive;}public java.util.UUID getUuid(){return new java.util.UUID(1,2);}public boolean hasStatusEffect(int type){return nausea;}}
public static class Options {public double scale=1;public Options getDistortionEffectScale(){return this;}public Double getValue(){return scale;}}}''',
'net/minecraft/entity/effect/StatusEffects.java':'''public class StatusEffects {public static final int NAUSEA=9;}''',
'net/minecraft/util/math/MathHelper.java':'''public class MathHelper {public static float lerp(float delta,float from,float to){return from+delta*(to-from);}}''',
'net/minecraft/client/render/Camera.java':'''public class Camera {}''',
'net/minecraft/client/gui/DrawContext.java':'''public class DrawContext {public int flushes;public void draw(){flushes++;}}''',
'com/mojang/blaze3d/systems/RenderSystem.java':'''public class RenderSystem {public static boolean alpha=true;public static void colorMask(boolean r,boolean g,boolean b,boolean a){if(!r||!g||!b)throw new AssertionError();alpha=a;}}''',
'net/minecraft/client/render/GameRenderer.java':'''public class GameRenderer implements dev.goldcraft.client.mixin.GameRendererAccessor {public int ticks,calls;public float overlay;public boolean fail;
public int goldcraft$ticks(){return ticks;}public void goldcraft$renderNausea(net.minecraft.client.gui.DrawContext c,float intensity){if(com.mojang.blaze3d.systems.RenderSystem.alpha)throw new AssertionError("additive alpha must be masked");calls++;overlay=intensity;if(fail)throw new IllegalStateException("probe");}
public double goldcraft$fov(Camera c,float d,boolean v){return 70;}public void goldcraft$renderHand(Camera c,float d,org.joml.Matrix4f m){}}''',
}
for name,body in adapters.items():
    f=src/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('package '+name.rsplit('/',1)[0].replace('/','.')+';\n'+body+'\n')
runtime=json.loads((R/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
java=R/'.tools/java/jdk-21.0.12.1+1/bin';runs=[]
def run(name,exe,args):
    response=out/(name+'.args');response.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args),encoding='utf-8')
    result=subprocess.run([str(java/exe),'@'+str(response)],cwd=out,capture_output=True,timeout=90)
    (out/(name+'.log')).write_bytes(result.stdout+result.stderr);runs.append(dict(name=name,exitCode=result.returncode))
    print(name,result.returncode,(result.stdout+result.stderr).decode(errors='replace')[-1600:]);assert result.returncode==0,name
production=[source/n for n in ['neoforge/src/client/java/dev/goldcraft/client/ViewDistortionExporter.java','neoforge/src/client/java/dev/goldcraft/client/mixin/GameRendererAccessor.java',
    'neoforge/src/main/java/dev/goldcraft/bridge/Wire.java','neoforge/src/main/java/dev/goldcraft/bridge/NativeArmorProtocol.java','neoforge/src/main/java/dev/goldcraft/bridge/FramePacer.java']]
cp=os.pathsep.join(map(str,[classes,*runtime['classpath']]))
run('compile-publisher','javac.exe',['--release','21','-proc:none','-cp',cp,'-d',classes,*src.rglob('*.java'),*production,R/'tests/view-distortion/DistortionProbe.java'])
run('publisher','java.exe',['-ea','-cp',cp,'dev.goldcraft.client.DistortionProbe'])
game=list((R/'neoforge/.gradle/loom-cache/minecraftMaven/net/minecraft').glob('neoforge-21.1.256-minecraft-merged-*/1.21.1-loom.mappings.*/neoforge-21.1.256-*-neoforge-21.1.256.jar'));assert len(game)==1;game=game[0]
fixtures=[R/'tests/view-distortion/DistortionTransform.java',*[R/'tests/nightfall-blade'/n for n in ('BladeMixinService.java','BladeProperties.java')]]
for kind in ('development','production'):
    target=transform/kind;services=target/'META-INF/services';services.mkdir(parents=True,exist_ok=True)
    (services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n');(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
    (target/'distortion.mixins.json').write_text(json.dumps(dict(required=True,package='dev.goldcraft.client.mixin',compatibilityLevel='JAVA_21',client=['GameRendererAccessor'])))
    if kind=='development':
        target_game=game;classpath=os.pathsep.join(map(str,[target,game,*runtime['classpath']]))
        mixins=[production[1]]
    else:
        libraries=list((R/'.tools/neoforge-runtime/21.1.256/libraries').rglob('*.jar'));target_game=next(f for f in libraries if f.name=='client-1.21.1-20240808.144430-srg.jar')
        classpath=os.pathsep.join(map(str,[target,a.production_jar.resolve(),target_game,*libraries]));mixins=[]
    run('compile-'+kind,'javac.exe',['--release','21','-proc:none','-cp',classpath,'-d',target,*fixtures,*mixins])
    run('transform-'+kind,'java.exe',['-cp',classpath,'probe.DistortionTransform',target_game,out,kind])
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,sourceHashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*production,*fixtures,a.production_jar.resolve()]},boundary='Production publisher with observing game APIs; actual development/shipped Sponge GameRenderer accessor verification; no live gameplay.'),indent=2)+'\n')
