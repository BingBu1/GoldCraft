"""Verify NightFall Murasama optional meshes with its exact local class and JSON.

Uses existing Java21/runtime dependencies and authored CPU adapters. Reads the
pinned local NightFall JAR at execution time. No third-party method body/asset is
published; generated classes/resources/logs stay in workspace analysis. This is
an actual Sponge and renderer-method regression, not a game/FML/GL session.
"""
from pathlib import Path
import argparse
import json
import hashlib
import subprocess
import zipfile
import os

source=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace',type=Path,default=source)
parser.add_argument('--output',type=Path,default=Path('analysis/murasama-mesh'))
a=parser.parse_args();r=a.workspace.resolve();o=(r/a.output).resolve()
if not o.is_relative_to(r/'analysis'):raise ValueError('Output must remain inside workspace analysis')
for path in [o,*o.parents]:
 if path.is_symlink() or path.is_junction():raise ValueError('Refusing redirected output')
 if path==r:break
o.mkdir(parents=True,exist_ok=True);s=o/'src';c=o/'classes';c.mkdir(exist_ok=True)
jar=r/'sandbox/neoforge-cs-client-b/mods/NightFall-1.21.1-Neoforge-3.4.0.jar'
if hashlib.sha256(jar.read_bytes()).hexdigest()!='ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c':raise ValueError('NightFall producer differs from verified3.4.0')
d=json.loads((r/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
if not any(Path(p).name=='mixinextras-neoforge-0.5.5.jar' for p in d['classpath']):raise ValueError('Expected runtime MixinExtras0.5.5')
files={
'net/minecraft/resources/ResourceLocation.java':'''package net.minecraft.resources;public record ResourceLocation(String namespace,String path){public static ResourceLocation parse(String x){var p=x.split(":",2);return new ResourceLocation(p[0],p[1]);}public String getNamespace(){return namespace;}public String getPath(){return path;}public String toString(){return namespace+":"+path;}}''',
'yesman/epicfight/client/renderer/patched/item/RenderItemBase.java':'''package yesman.epicfight.client.renderer.patched.item;public class RenderItemBase{public RenderItemBase(com.google.gson.JsonElement json){}}''',
'yesman/epicfight/api/asset/AssetAccessor.java':'''package yesman.epicfight.api.asset;public interface AssetAccessor<T>{T get();}''',
'yesman/epicfight/api/client/model/Mesh.java':'''package yesman.epicfight.api.client.model;public class Mesh {public static class RenderProperties{}}''',
'yesman/epicfight/api/client/model/StaticMesh.java':'''package yesman.epicfight.api.client.model;public class StaticMesh extends Mesh{}''',
'yesman/epicfight/api/client/model/SkinnedMesh.java':'''package yesman.epicfight.api.client.model;
import java.util.*;import com.mojang.blaze3d.vertex.PoseStack;import net.minecraft.client.renderer.*;import yesman.epicfight.api.model.Armature;import yesman.epicfight.api.utils.math.OpenMatrix4f;
public class SkinnedMesh extends StaticMesh {
 public final String id;public static final List<String> draws=new ArrayList<>();
 public SkinnedMesh(String name){id=name;}public SkinnedMesh(Map a,Map b,SkinnedMesh base,Mesh.RenderProperties p){id="loaded";}
 public void draw(PoseStack pose,MultiBufferSource buffers,RenderType type,int light,float r,float g,float b,float a,int overlay,Armature arm,OpenMatrix4f[] matrices){draws.add(id+"|"+type+"|"+light+"|"+r+"|"+g+"|"+b+"|"+a+"|"+overlay+"|"+pose.depth+"|"+arm.poses);}
}''',
'yesman/epicfight/api/client/model/Meshes.java':'''package yesman.epicfight.api.client.model;
import java.util.*;import java.util.function.*;import yesman.epicfight.api.asset.*;
public final class Meshes {
 @FunctionalInterface public interface MeshContructor<T extends StaticMesh>{T invoke(Map a,Map b,T base,Mesh.RenderProperties p);}
 public static final List<String> creates=new ArrayList<>(),gets=new ArrayList<>();public static String failId,returnNullId;
 public record MeshAccessor(String name) implements AssetAccessor<SkinnedMesh>{
  public static MeshAccessor create(String ns,String path,Function<JsonAssetLoader,SkinnedMesh> load){var id=ns+":"+path;creates.add(id);return new MeshAccessor(id);}
  public SkinnedMesh get(){gets.add(name);if(name.equals(failId))throw new IllegalStateException("fixture asset load failure");return name.equals(returnNullId)?null:new SkinnedMesh(name);}
 }
}''',
'yesman/epicfight/api/asset/JsonAssetLoader.java':'''package yesman.epicfight.api.asset;import yesman.epicfight.api.client.model.*;public class JsonAssetLoader{public SkinnedMesh loadSkinnedMesh(Meshes.MeshContructor<SkinnedMesh> maker){return maker.invoke(null,null,null,null);}}''',
'net/minecraft/core/component/DataComponentType.java':'''package net.minecraft.core.component;public class DataComponentType{}''',
'net/minecraft/core/component/DataComponents.java':'''package net.minecraft.core.component;public class DataComponents{public static final DataComponentType CUSTOM_DATA=new DataComponentType();}''',
'net/minecraft/nbt/CompoundTag.java':'''package net.minecraft.nbt;public record CompoundTag(int sheath){public int getInt(String key){if(!key.equals("murasama_sheath"))throw new AssertionError(key);return sheath;}}''',
'net/minecraft/world/item/component/CustomData.java':'''package net.minecraft.world.item.component;public record CustomData(int sheath){public static final CustomData EMPTY=new CustomData(0);public net.minecraft.nbt.CompoundTag copyTag(){return new net.minecraft.nbt.CompoundTag(sheath);}}''',
'net/minecraft/world/item/ItemStack.java':'''package net.minecraft.world.item;public record ItemStack(int sheath){public Object getOrDefault(net.minecraft.core.component.DataComponentType key,Object fallback){return new net.minecraft.world.item.component.CustomData(sheath);}}''',
'net/minecraft/world/InteractionHand.java':'''package net.minecraft.world;public enum InteractionHand{MAIN_HAND,OFF_HAND}''',
'com/mojang/blaze3d/vertex/PoseStack.java':'''package com.mojang.blaze3d.vertex;public class PoseStack{public int depth,pushes,pops;public void pushPose(){depth++;pushes++;}public void popPose(){if(depth<=0)throw new AssertionError("underflow");depth--;pops++;}}''',
'yesman/epicfight/api/utils/math/OpenMatrix4f.java':'''package yesman.epicfight.api.utils.math;public class OpenMatrix4f{}''',
'yesman/epicfight/api/animation/Pose.java':'''package yesman.epicfight.api.animation;public class Pose{}''',
'yesman/epicfight/api/animation/Animator.java':'''package yesman.epicfight.api.animation;public class Animator{public Pose getPose(float partial){return new Pose();}}''',
'yesman/epicfight/api/model/Armature.java':'''package yesman.epicfight.api.model;public class Armature{public int poses;public void setPose(yesman.epicfight.api.animation.Pose p){poses++;}public yesman.epicfight.api.utils.math.OpenMatrix4f[] getPoseMatrices(){return new yesman.epicfight.api.utils.math.OpenMatrix4f[0];}}''',
'yesman/epicfight/world/capabilities/entitypatch/LivingEntityPatch.java':'''package yesman.epicfight.world.capabilities.entitypatch;public class LivingEntityPatch<T>{public final yesman.epicfight.api.model.Armature arm=new yesman.epicfight.api.model.Armature();public yesman.epicfight.api.model.Armature getArmature(){return arm;}public yesman.epicfight.api.animation.Animator getAnimator(){return new yesman.epicfight.api.animation.Animator();}}''',
'yesman/epicfight/world/capabilities/entitypatch/player/PlayerPatch.java':'''package yesman.epicfight.world.capabilities.entitypatch.player;public class PlayerPatch<T> extends yesman.epicfight.world.capabilities.entitypatch.LivingEntityPatch<T>{}''',
'net/minecraft/client/renderer/MultiBufferSource.java':'''package net.minecraft.client.renderer;public interface MultiBufferSource{}''',
'net/minecraft/client/renderer/RenderType.java':'''package net.minecraft.client.renderer;public record RenderType(String value){public static RenderType entityTranslucent(net.minecraft.resources.ResourceLocation id){return new RenderType("translucent:"+id);}public static RenderType entityTranslucentEmissive(net.minecraft.resources.ResourceLocation id){return new RenderType("emissive:"+id);}}''',
'net/minecraft/client/renderer/texture/OverlayTexture.java':'''package net.minecraft.client.renderer.texture;public class OverlayTexture{public static final int NO_OVERLAY=655360;}'''
}
for name,data in files.items():
 p=s/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(data,encoding='utf-8')
services=c/'META-INF/services';services.mkdir(parents=True,exist_ok=True)
(services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
(c/'murasama-only.mixins.json').write_text(json.dumps({'required':True,'package':'dev.goldcraft.client.mixin','compatibilityLevel':'JAVA_21','client':['NightfallMurasamaMeshMixin'],'injectors':{'defaultRequire':1}}))
with zipfile.ZipFile(jar) as z:(o/'hf_murasama.json').write_bytes(z.read('assets/efn/item_skins/hf_murasama.json'))
cp=os.pathsep.join([str(c),str(jar),*d['classpath']]);java=r/'.tools/java/jdk-21.0.12.1+1/bin'
mixin=source/'neoforge/src/client/java/dev/goldcraft/client/mixin/NightfallMurasamaMeshMixin.java'
fixtures=[*sorted((source/'tests/murasama-mesh').glob('*.java')),*[r/'tests/nightfall-blade'/name for name in ['BladeMixinService.java','BladeProperties.java']]]
runs=[]
def run(name,exe,args):
 argfile=o/(name+'.args')
 argfile.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in args),encoding='utf-8')
 p=subprocess.run([str(java/exe),'@'+str(argfile)],cwd=o,capture_output=True,timeout=90)
 (o/(name+'.log')).write_bytes(p.stdout+p.stderr);runs.append({'name':name,'exitCode':p.returncode})
 if p.returncode:raise RuntimeError(f'{name} failed; inspect {o/(name+".log")}')
 print('PASS '+name)
try:
 run('compile-production','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',os.pathsep.join(d['classpath']),'-d',o/'production-classes',mixin])
 run('compile-fixtures','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',c,*sorted(s.rglob('*.java')),*fixtures,mixin])
 run('transform','java.exe',['-Dlog4j.configurationFile=none','-cp',cp,'probe.TransformMurasama',jar,c])
 run('regression','java.exe',['-cp',cp,'probe.MurasamaRegression',o/'hf_murasama.json'])
 print((o/'regression.log').read_text(encoding='utf-8').strip())
finally:
 inputs=[jar,mixin,Path(__file__),*fixtures]
 (o/'result.json').write_text(json.dumps({'runs':runs,'inputs':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'scope':'actual original constructor/render + Sponge; CPU resource/draw/pose adapters; no game/FML/GL'},indent=2),encoding='utf-8')
