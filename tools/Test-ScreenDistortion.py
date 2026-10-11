"""Check pinned NightFall screen-distortion expiry with its actual local classes.

The original effect and lifecycle classes are extracted only into analysis.
Authored adapters replace game/GL boundaries. No game process or FML starts.
The exact production expiration predicate is copied from VixPostCapture at run time.
"""
from pathlib import Path
import argparse, hashlib, io, json, re, subprocess, zipfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parents[1])
parser.add_argument('--output',type=Path,default=Path('analysis/screen-distortion-regression'))
args=parser.parse_args()
root=args.workspace.resolve()
out=root/args.output
src, classes = out / 'src', out / 'classes'
assert out.resolve().is_relative_to(root / 'analysis')
assert not any(p.is_symlink() or p.is_junction() for p in [out, *out.parents])
classes.mkdir(parents=True, exist_ok=True)
jar = root / 'sandbox/neoforge-cs-client-b/mods/NightFall-1.21.1-Neoforge-3.4.0.jar'
digest = lambda b: hashlib.sha256(b).hexdigest()
assert digest(jar.read_bytes()) == 'ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c'
hashes = {}
with zipfile.ZipFile(jar) as z:
    vix_bytes=z.read('META-INF/jarjar/vix-1.0.9.jar')
    assert digest(vix_bytes)=='0d6d3aee5030abcae0247ef2d7ccc8267a2a56513d0009941a107ed36f4f6aca'
    with zipfile.ZipFile(io.BytesIO(vix_bytes)) as v:
        for archive,names in [(z,['com/hm/efn/client/screeneffect/ScreenDistortionEffect', 'com/hm/efn/client/screeneffect/ScreenDistortionEffect$Distortion_Pipeline']), (v,['com/guhao/vix/client/screeneffect/ScreenEffectBase','com/guhao/vix/client/screeneffect/ScreenEffectBase$SE_Pipeline'])]:
            for name in names:
                raw=archive.read(name+'.class'); path=classes/(name+'.class');path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw);hashes[name]=digest(raw)
files={
'net/minecraft/resources/ResourceLocation.java': '''package net.minecraft.resources;public record ResourceLocation(String namespace,String path){}''',
'net/minecraft/world/phys/Vec3.java': '''package net.minecraft.world.phys;public class Vec3{public final double x,y,z;public Vec3(double x,double y,double z){this.x=x;this.y=y;this.z=z;}public Vec3 subtract(Vec3 b){return new Vec3(x-b.x,y-b.y,z-b.z);}public double length(){return Math.sqrt(x*x+y*y+z*z);}}''',
'net/minecraft/client/Camera.java': '''package net.minecraft.client;public class Camera{public net.minecraft.world.phys.Vec3 getPosition(){return new net.minecraft.world.phys.Vec3(0,0,0);}}''',
'com/mojang/blaze3d/pipeline/RenderTarget.java': '''package com.mojang.blaze3d.pipeline;public class RenderTarget{}''',
'net/minecraft/client/Minecraft.java': '''package net.minecraft.client;public class Minecraft{private static final Minecraft INSTANCE=new Minecraft();private final com.mojang.blaze3d.pipeline.RenderTarget target=new com.mojang.blaze3d.pipeline.RenderTarget();public static Minecraft getInstance(){return INSTANCE;}public com.mojang.blaze3d.pipeline.RenderTarget getMainRenderTarget(){return target;}}''',
'com/guhao/vix/client/targets/TargetManager.java': '''package com.guhao.vix.client.targets;public class TargetManager{private static final com.mojang.blaze3d.pipeline.RenderTarget target=new com.mojang.blaze3d.pipeline.RenderTarget();public static com.mojang.blaze3d.pipeline.RenderTarget getTarget(net.minecraft.resources.ResourceLocation id){return target;}}''',
'com/guhao/vix/client/shaderpasses/PostPassBase.java': '''package com.guhao.vix.client.shaderpasses;public class PostPassBase{public PostPassBase(String name,Object unused){}public void process(com.mojang.blaze3d.pipeline.RenderTarget a,com.mojang.blaze3d.pipeline.RenderTarget b){}}''',
'com/guhao/vix/util/OjangUtils.java': '''package com.guhao.vix.util;public class OjangUtils{public static net.minecraft.resources.ResourceLocation newRL(String a,String b){return new net.minecraft.resources.ResourceLocation(a,b);}}''',
'com/guhao/vix/client/pipeline/PostEffectPipelines.java': '''package com.guhao.vix.client.pipeline; import net.minecraft.resources.ResourceLocation; public class PostEffectPipelines { public static abstract class Pipeline {public int priority;public Pipeline(ResourceLocation id){}public abstract void PostEffectHandler();}}''',
'net/minecraft/util/Mth.java': '''package net.minecraft.util;public class Mth {private static final float[] SIN=new float[65536];static{for(int i=0;i<65536;i++)SIN[i]=(float)Math.sin(i*Math.PI*2.0/65536.0);}public static float sin(float x){return SIN[(int)(x*10430.378f)&65535];}public static float sqrt(float x){return (float)Math.sqrt(x);}}''',
'com/hm/efn/registries/PostPasses.java': '''package com.hm.efn.registries; public class PostPasses{public static final com.guhao.vix.client.shaderpasses.PostPassBase blit=new com.guhao.vix.client.shaderpasses.PostPassBase("blit",null);public static final com.hm.efn.client.shaderpasses.ScreenDistortion screen_distortion=new com.hm.efn.client.shaderpasses.ScreenDistortion();}''',
'com/hm/efn/client/shaderpasses/ScreenDistortion.java': '''package com.hm.efn.client.shaderpasses;import com.mojang.blaze3d.pipeline.RenderTarget;public class ScreenDistortion{public static float strength,frequency,time,progress; public void process(RenderTarget a,RenderTarget b,float s,float f,float t,float p){strength=s;frequency=f;time=t;progress=p;}}''',
'net/minecraft/world/phys/AABB.java': '''package net.minecraft.world.phys;public class AABB{public AABB(double a,double b,double c,double d,double e,double f){}}''',
'net/minecraft/client/renderer/culling/Frustum.java': '''package net.minecraft.client.renderer.culling;public class Frustum{public boolean isVisible(net.minecraft.world.phys.AABB a){return true;}}''',
'StageFailureProbe.java': '''import com.hm.efn.client.screeneffect.ScreenDistortionEffect;import com.hm.efn.client.shaderpasses.ScreenDistortion;import net.minecraft.world.phys.Vec3;import dev.goldcraft.bridge.PostEffectPlan;
public class StageFailureProbe{
static int cases,checks,finiteCases,tailCases;static void check(boolean x,String why){checks++;if(!x)throw new AssertionError(why);}
static float[] run(ScreenDistortionEffect e,float partial){e.ppl.setPartialTicks(partial);e.ppl.PostEffectHandler();return new float[]{ScreenDistortion.time,0,0,ScreenDistortion.strength,ScreenDistortion.frequency,ScreenDistortion.progress,0,0};}
static boolean stage(float[] p){try{new PostEffectPlan.Stage(5,100,0,0,p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);return true;}catch(IllegalArgumentException ex){return false;}}
public static void main(String[] args){
System.out.println("actual effect="+ScreenDistortionEffect.class.getProtectionDomain().getCodeSource().getLocation());
System.out.println("actual lifecycle="+com.guhao.vix.client.screeneffect.ScreenEffectBase.class.getProtectionDomain().getCodeSource().getLocation());
for(int life:new int[]{1,2,6,20,60})for(int age=0;age<=life;age++)for(float partial:new float[]{0,.0001f,.01f,.125f,.5f,.9375f,.999f}){
var e=new ScreenDistortionEffect(new Vec3(0,0,0),life,.3f,12,4.8f);e.age=age;var p=run(e,partial);cases++;float normalized=e.getNormalizedAgeWithPartialTicks();
check(!e.shouldRemoved(),"tick lifecycle still retains age<=lifetime");
boolean expired=ExpiredGuard.expiredScreenDistortion(age,life,normalized);
if(age==life&&partial>0){tailCases++;check(Float.isNaN(p[3]),"tail strength must be NaN");check(!stage(p),"strict Stage rejects original producer NaN");check(expired,"actual production guard skips exactly expired tail");}
else{finiteCases++;check(!expired,"actual guard preserves finite domain");check(stage(p),"finite valid domain retained");var second=run(e,partial);for(int i=0;i<8;i++)check(Float.floatToRawIntBits(p[i])==Float.floatToRawIntBits(second[i]),"finite unchanged producer bytes");}
if(life==6&&age>=5)System.out.printf("life=%d age=%d partial=%s normalized=%s time=%s strength=%s frequency=%s progress=%s stage=%s removed=%s%n",life,age,partial,normalized,p[0],p[3],p[4],p[5],stage(p),e.shouldRemoved());
e.age=life;e.tick();check(e.shouldRemoved(),"removed only next tick");
}
for(int age:new int[]{-1,0,6})for(int life:new int[]{-6,0})for(float partial:new float[]{0,.5f}){var e=new ScreenDistortionEffect(new Vec3(0,0,0),life,.3f,12,4.8f);e.age=age;e.ppl.setPartialTicks(partial);check(!ExpiredGuard.expiredScreenDistortion(age,life,e.getNormalizedAgeWithPartialTicks()),"invalid lifetime not silently expired");}
check(!ExpiredGuard.expiredScreenDistortion(-6,6,2),"negative age not silently expired");
for(float value:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY})check(!ExpiredGuard.expiredScreenDistortion(6,6,value),"nonfinite age not silently expired");
var b=new dev.goldcraft.bridge.ParticleSnapshot.Batch(-1,0,new byte[96]);
var left=new PostEffectPlan.Stage(1,0,0,1,0,0,0,1,0,0,0,0);var right=new PostEffectPlan.Stage(2,10,1,1,0,0,0,1,0,0,0,0);
var expected=PostEffectPlan.encode(9,1,1,1,22,java.util.List.of(left,right),java.util.List.of(0),java.util.List.of(b,b));
for(float partial:new float[]{0,.01f,.5f,.999f}){var e=new ScreenDistortionEffect(new Vec3(0,0,0),6,.3f,12,4.8f);e.age=6;e.ppl.setPartialTicks(partial);var stages=new java.util.ArrayList<PostEffectPlan.Stage>();stages.add(left);boolean expired=ExpiredGuard.expiredScreenDistortion(e.age,e.lifetime,e.getNormalizedAgeWithPartialTicks());if(!expired){var p=run(e,partial);stages.add(new PostEffectPlan.Stage(5,100,1,0,p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]));}stages.add(right);var wire=PostEffectPlan.encode(9,1,1,1,22,stages,java.util.List.of(0),java.util.List.of(b,b));if(expired)check(java.util.Arrays.equals(wire,expected),"neighbor stages/reset/mesh preserved byte-for-byte");else check(stages.size()==3,"exact endpoint retains zero-strength stage");}
System.out.printf("RESULT cases=%d checks=%d finiteCases=%d tailCases=%d%n",cases,checks,finiteCases,tailCases);
}}
'''
}
capture_path=root/'neoforge/src/client/java/dev/goldcraft/client/VixPostCapture.java'
capture=capture_path.read_text(encoding='utf-8')
guard=re.search(r'    static boolean expiredScreenDistortion\(int age,int lifetime,float normalizedAge\) \{\s*return [^;]+;\s*\}',capture)
assert guard, 'Production guard signature drift'
files['ExpiredGuard.java']='public class ExpiredGuard {\n'+guard.group(0)+'\n}\n'
hashes['productionGuardMethod']=digest(guard.group(0).encode())
hashes['VixPostCaptureJava']=digest(capture_path.read_bytes())
sources=[]
for name,text in files.items():
    p=src/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8');sources.append(p)
java=root/'.tools/java/jdk-21.0.12.1+1/bin'
cp=str(classes)
sources += [root/'neoforge/src/main/java/dev/goldcraft/bridge'/n for n in ['PostEffectPlan.java','Wire.java','ParticleSnapshot.java']]
runs=[]
for name,exe,args in [('compile','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',str(classes),*map(str,sources)]),('probe','java.exe',['-cp',cp,'StageFailureProbe'])]:
    p=subprocess.run([str(java/exe),*args],cwd=out,capture_output=True,timeout=60)
    (out/(name+'.log')).write_bytes(p.stdout+p.stderr);print(p.stdout.decode('utf-8',errors='replace'));print(p.stderr.decode('utf-8',errors='replace'))
    runs.append({'name':name,'exitCode':p.returncode})
    if p.returncode:break
for name,expected in hashes.items():
    if name.startswith('com/'):
        assert digest((classes/(name+'.class')).read_bytes())==expected, 'Producer class modified: '+name
(out/'result.json').write_text(json.dumps({'runs':runs,'unchangedExtractedClassesAndProductionGuard':hashes,'inputs':{str(p.relative_to(root)):digest(p.read_bytes()) for p in [capture_path,*sources[-3:]]},'scope':'Unmodified NightFall effect constructor and handler plus VIX lifecycle and SE_Pipeline, extracted exact production expiration guard, actual production Stage and encode; authored game/GL adapters, Mth sine lookup/sqrt adapter; no game process or FML.'},indent=2),encoding='utf-8')
raise SystemExit(runs[-1]['exitCode'])
