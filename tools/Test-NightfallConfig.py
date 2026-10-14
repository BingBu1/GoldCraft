"""Verify the actual NightFall config redirect and isolated side behavior.

Reads pinned third-party classes locally; no animation code is redistributed.
Sponge transformation is real. Handler execution uses side/config adapters;
this does not replace dedicated-server gameplay validation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
out=(ROOT/args.output).resolve()
if not out.is_relative_to(ROOT/'analysis'):
    raise ValueError('Evidence must stay in analysis')
for path in [out,*out.parents]:
    if path.is_symlink() or path.is_junction():raise ValueError('Redirected evidence path')
    if path==ROOT:break
out.mkdir(exist_ok=False,parents=True)
classes=out/'classes';classes.mkdir()
jar=ROOT/'sandbox/neoforge-cs-client-b/mods/NightFall-1.21.1-Neoforge-3.4.0.jar'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if digest(jar)!='ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c':
    raise ValueError('Unverified NightFall producer')
runtime=json.loads((ROOT/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
cp=os.pathsep.join([str(classes),str(jar),*runtime['classpath']])
java=ROOT/'.tools/java/jdk-21.0.12.1+1/bin'
mixin=ROOT/'neoforge/src/main/java/dev/goldcraft/mixin/NightfallMurasamaConfigMixin.java'
fixtures=[ROOT/'tests/nightfall-config/ConfigTransform.java',
          *[ROOT/'tests/nightfall-blade'/p for p in ('BladeMixinService.java','BladeProperties.java')]]
services=classes/'META-INF/services';services.mkdir(parents=True)
(services/'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
(services/'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
(classes/'config-only.mixins.json').write_text(json.dumps({
    'required':True,'package':'dev.goldcraft.mixin','compatibilityLevel':'JAVA_21',
    'mixins':['NightfallMurasamaConfigMixin'],'injectors':{'defaultRequire':1}}))
runs=[]
def run(name,exe,arguments):
    argfile=out/(name+'.args')
    argfile.write_text('\n'.join('"'+str(v).replace('\\','/').replace('"','\\"')+'"' for v in arguments))
    proc=subprocess.run([str(java/exe),'@'+str(argfile)],cwd=out,capture_output=True,timeout=60)
    (out/(name+'.log')).write_bytes(proc.stdout+proc.stderr)
    runs.append({'name':name,'exitCode':proc.returncode})
    if proc.returncode:raise RuntimeError(f'{name} failed: see {out/(name+".log")}')
    print('PASS '+name)
try:
    run('compile','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',classes,*fixtures,mixin])
    for side in ('SERVER','CLIENT'):
        run('transform-'+side.lower(),'java.exe',['-cp',cp,'probe.ConfigTransform',jar,classes,side])
    adapters={
        'net/neoforged/fml/loading/FMLEnvironment.java':
        'package net.neoforged.fml.loading;public class FMLEnvironment{public static net.neoforged.api.distmarker.Dist dist=net.neoforged.api.distmarker.Dist.CLIENT;}',
        'net/neoforged/api/distmarker/Dist.java':
        'package net.neoforged.api.distmarker;public enum Dist{CLIENT,DEDICATED_SERVER;public boolean isDedicatedServer(){return this==DEDICATED_SERVER;}}',
        'net/neoforged/neoforge/common/ModConfigSpec.java':
        'package net.neoforged.neoforge.common;public class ModConfigSpec{public static class BooleanValue{public int reads,defaults;public boolean loaded,value;public Object get(){reads++;if(!loaded)throw new IllegalStateException("unloaded CLIENT config");return value;}public Object getDefault(){defaults++;return true;}}}',
        'probe/ConfigBehavior.java':
        '''package probe;
import net.neoforged.fml.loading.FMLEnvironment;
import net.neoforged.neoforge.common.ModConfigSpec;
public class ConfigBehavior{public static void main(String[] args)throws Exception{
 var method=Class.forName("probe.InjectedConfig").getMethod("apply",ModConfigSpec.BooleanValue.class);
 var option=new ModConfigSpec.BooleanValue();FMLEnvironment.dist=net.neoforged.api.distmarker.Dist.DEDICATED_SERVER;
 if(!Boolean.TRUE.equals(method.invoke(null,option))||option.reads!=0||option.defaults!=1)throw new AssertionError("dedicated touched unloaded config");
 FMLEnvironment.dist=net.neoforged.api.distmarker.Dist.CLIENT;option.loaded=true;option.value=false;
 if(!Boolean.FALSE.equals(method.invoke(null,option))||option.reads!=1||option.defaults!=1)throw new AssertionError("client false option lost");
 option.value=true;
 if(!Boolean.TRUE.equals(method.invoke(null,option))||option.reads!=2)throw new AssertionError("client true option lost");
 System.out.println("PASS exact injected handler: unloaded dedicated default; both loaded client choices preserved");
}}'''}
    sources=[]
    for name,data in adapters.items():
        path=out/'adapters'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(data);sources.append(path)
    adapter_classes=out/'adapter-classes'
    run('compile-adapters','javac.exe',['--release','21','-proc:none','-d',adapter_classes,*sources])
    run('behavior','java.exe',['-cp',os.pathsep.join([str(adapter_classes),str(classes)]),'probe.ConfigBehavior'])
finally:
    (out/'result.json').write_text(json.dumps({'runs':runs,'inputs':{str(p):digest(p) for p in [jar,mixin,Path(__file__),*fixtures]},
        'scope':'Real Sponge transformation; exact handler bytecode with CPU side/config adapters; no FML/gameplay'},indent=2))
