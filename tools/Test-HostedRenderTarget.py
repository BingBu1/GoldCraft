"""Actual Minecraft target getter and TACZ stencil helper against a hidden GPU context."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,zipfile
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--tacz',type=Path,required=True);p.add_argument('--production-jar',type=Path);a=p.parse_args()
out=a.output.resolve()
if not out.is_relative_to(R/'build') or any(q.is_symlink() or q.is_junction() for q in (out,*out.parents)):raise ValueError('Use a plain workspace build directory')
out.mkdir(parents=True,exist_ok=True);java=R/'.tools/java/jdk-21.0.12.1+1/bin';runtime=json.loads((R/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
game=next((R/'neoforge/.gradle/loom-cache/minecraftMaven/net/minecraft').glob('neoforge-21.1.256-minecraft-merged-*/1.21.1-loom.mappings.*/neoforge-21.1.256-*-neoforge-21.1.256.jar'))
sources=[R/'neoforge/src/client/java/dev/goldcraft/client'/n for n in ['HostedRenderTarget.java','HostedStencilState.java','mixin/HostedFramebufferMixin.java']]
fixtures=[R/'tests/hosted-target'/n for n in ['TargetProbe.java','ActualScopeCalls.java','TargetTransform.java']];runs=[]
def run(name,exe,args):
    response=out/(name+'.args');response.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args),encoding='utf-8')
    r=subprocess.run([str(java/exe),'@'+str(response)],cwd=out,capture_output=True,timeout=120);(out/(name+'.log')).write_bytes(r.stdout+r.stderr);runs.append(dict(name=name,exitCode=r.returncode));print(name,r.returncode,(r.stdout+r.stderr).decode(errors='replace')[-2200:]);assert r.returncode==0,name
for mode in ['development']+(['production'] if a.production_jar else []):
    base=out/mode;classes=base/'classes';src=base/'src';transform=base/'transform';classes.mkdir(parents=True,exist_ok=True);src.mkdir(exist_ok=True);transform.mkdir(exist_ok=True)
    prod=mode=='production';client='Minecraft' if prod else 'MinecraftClient';fb='com.mojang.blaze3d.pipeline.RenderTarget' if prod else 'net.minecraft.client.gl.Framebuffer';getter='getMainRenderTarget' if prod else 'getFramebuffer'
    files={f'net/minecraft/client/{client}.java':f'package net.minecraft.client;import {fb};public class {client} {{public static final boolean IS_SYSTEM_MAC=false,ON_OSX=false;private static final {client} INSTANCE=new {client}();public {fb} framebuffer;public static {client} getInstance(){{return INSTANCE;}} public {fb} {getter}(){{throw new AssertionError("Real getter required");}}}}',
           'com/tacz/guns/compat/optifine/OptifineCompat.java':'package com.tacz.guns.compat.optifine;public final class OptifineCompat{public static boolean isOptifineInstalled(){return false;}}'}
    for n,text in files.items():f=src/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(text,encoding='utf-8')
    if prod:
        f=src/f'net/minecraft/client/{client}.java';f.write_text(f.read_text().replace(' framebuffer;',' mainRenderTarget;'),encoding='utf-8')
    probe=(R/'tests/hosted-target/TargetProbe.java').read_text()
    if prod:probe=probe.replace('net.minecraft.client.MinecraftClient','net.minecraft.client.Minecraft').replace('MinecraftClient','Minecraft').replace('net.minecraft.client.gl.Framebuffer','com.mojang.blaze3d.pipeline.RenderTarget').replace('net.minecraft.client.gl.SimpleFramebuffer','com.mojang.blaze3d.pipeline.TextureTarget').replace('SimpleFramebuffer','TextureTarget').replace('Framebuffer','RenderTarget').replace('getRenderTarget()','getMainRenderTarget()').replace('.framebuffer','.mainRenderTarget').replace('.fbo','.frameBufferId').replace('.textureWidth','.width').replace('.textureHeight','.height').replace('.beginWrite(','.bindWrite(').replace('.delete()','.destroyBuffers()')
    (src/'TargetProbe.java').write_text(probe,encoding='utf-8')
    if prod:
        libraries=list((R/'.tools/neoforge-runtime/21.1.256/libraries').rglob('*.jar'));target_game=next(f for f in libraries if f.name=='neoforge-21.1.256-client.jar');libs=[target_game,*libraries]
        # Production helpers retain their shipped names and are loaded directly from the tested JAR.
        with zipfile.ZipFile(a.production_jar) as z:
            for n in z.namelist():
                if n.startswith('dev/goldcraft/client/Hosted') and n.endswith('.class') or n=='dev/goldcraft/client/mixin/HostedFramebufferMixin.class':
                    f=classes/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(z.read(n))
        compile_sources=[]
    else:target_game=game;libs=[game,*runtime['classpath']];compile_sources=sources[:2]
    cp=os.pathsep.join(map(str,[classes,*libs]));run(mode+'-compile','javac.exe',['--release','21','-proc:none','-cp',cp,'-d',classes,*src.rglob('*.java'),*compile_sources,R/'tests/hosted-target/ActualScopeCalls.java'])
    services=transform/'META-INF/services';services.mkdir(parents=True,exist_ok=True);(services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n');(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
    (transform/'hosted-target.mixins.json').write_text(json.dumps(dict(required=True,package='dev.goldcraft.client.mixin',compatibilityLevel='JAVA_21',client=['HostedFramebufferMixin'],injectors=dict(defaultRequire=1))))
    # Put real Minecraft before observer stubs while transforming; run the resulting getter against the stub only for GPU execution.
    tcp=os.pathsep.join(map(str,[transform,target_game,classes,*libs]));run(mode+'-compile-transform','javac.exe',['--release','21','-proc:none','-cp',tcp,'-d',transform,R/'tests/hosted-target/TargetTransform.java',*[R/'tests/nightfall-blade'/n for n in ['BladeMixinService.java','BladeProperties.java']],*([] if prod else [sources[2]])])
    run(mode+'-transform','java.exe',['-cp',tcp,'probe.TargetTransform',target_game,classes,mode,a.tacz.resolve()])
    run(mode+'-gpu','java.exe',['-Dorg.lwjgl.system.SharedLibraryExtractPath='+str(base/'natives'),'-cp',cp,'dev.goldcraft.client.TargetProbe'])
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [*sources,*fixtures,a.tacz,*([a.production_jar] if a.production_jar else [])]},boundary='Actual framebuffer and stencil helper with Minecraft singleton/Optifine/config observers; no live gameplay.'),indent=2)+'\n')
