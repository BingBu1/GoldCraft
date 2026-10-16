"""Check hosted relative input and actual pinned Epic camera behavior.

Uses production CreateMove/wire code with CPU engine adapters, extracts the user's
local Epic/Minecraft methods for the camera checks, and verifies indicator API
signatures. Does not distribute third-party code or run FML/GL/game input.
"""
from pathlib import Path
import argparse,subprocess,json,hashlib
source=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace',type=Path,default=source)
parser.add_argument('--output',type=Path,default=Path('analysis/host-look-check'))
parser.add_argument('--native-only',action='store_true',help='Run only the production CreateMove regression')
args=parser.parse_args();r=args.workspace.resolve();c=source;out=(r/args.output).resolve()
if not any(out.is_relative_to(r/p) for p in ('analysis','build')):raise ValueError('Output must be inside workspace analysis or build')
for p in [out,*out.parents]:
 if p.is_symlink() or p.is_junction():raise ValueError('Refusing redirected output')
 if p==r:break
out.mkdir(parents=True,exist_ok=True)
classes=out/'classes';src=out/'src';classes.mkdir(exist_ok=True);src.mkdir(exist_ok=True)
source=c/'native/client/plugin.cpp';s=source.read_text()
# Extract complete production functions, independent of neighboring helpers.
def function(signature):
 start=s.index(signature);end=s.index('{',start);depth=1;end+=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]+'\n'
body=''.join(function(f) for f in ('bool HasControl()','bool MinecraftFormWaiting()','void CreateMove('))
(out/'create_move.inc').write_text(body)
(out/'run.ps1').write_text('''param([string]$Root,[string]$Candidate,[string]$Output)
$ErrorActionPreference='Stop'
(Get-Process -Id $PID).PriorityClass='BelowNormal'
. (Join-Path $Root 'tools/SandboxPaths.ps1')
. (Join-Path $Root 'tools/ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$env:TEMP=$Output
$env:TMP=$Output
Push-Location $Output
try {
 & (Join-Path $compiler.Root 'bin/clang-cl.exe') /nologo --target=i686-pc-windows-msvc /std:c++20 /O2 /clang:-O3 /clang:-flto=thin /clang:-fuse-ld=lld /EHsc /UNDEBUG /DNOMINMAX /fp:precise /arch:AVX2 ("/I"+(Join-Path $Root 'native/include')) ("/I"+$Output) (Join-Path $Candidate 'tests/host-look/native.cpp') (Join-Path $Root 'native/common/wire.cpp') /Fe:probe.exe /link /MACHINE:X86 bcrypt.lib
 if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
 & (Join-Path $Output 'probe.exe')
 exit $LASTEXITCODE
}finally{Pop-Location}
''')
p=subprocess.run(['pwsh','-NoProfile','-File',str(out/'run.ps1'),'-Root',str(r),'-Candidate',str(c),'-Output',str(out)],cwd=out,capture_output=True,timeout=60)
(out/'native.log').write_bytes(p.stdout+p.stderr);print((p.stdout+p.stderr).decode(errors='replace')[-6000:])
(out/'native-result.json').write_text(json.dumps({'exitCode':p.returncode,'candidateSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'boundary':'Actual candidate CreateMove body and production wire encoding; simulated engine/window/transport, no game input.'},indent=2)+'\n')
if p.returncode: raise RuntimeError('native probe failed')
if args.native_only:raise SystemExit(0)

d=json.loads((r/'build/epicfight-maid/build.json').read_text(encoding='utf-8-sig'));dev=json.loads((r/'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
epic=r/'sandbox/neoforge-cs-client-b/mods/epic-fight-21.17.3.1-mc1.21.1-neoforge.jar'
assert hashlib.sha256(epic.read_bytes()).hexdigest()=='8b882554cf10086398340fbdc741819ee72a801a3adce516c7f4768326a39526'
mc=next(Path(p) for p in d['compileClasspath'] if Path(p).name=='client-1.21.1-20240808.144430-srg.jar')
files={
'dev/goldcraft/GoldCraft.java':'package dev.goldcraft; public class GoldCraft{public static final Logger LOGGER=new Logger(); public static class Logger{public void warn(String message,Throwable e){e.printStackTrace();}}}',
'net/neoforged/fml/ModList.java':'package net.neoforged.fml; public class ModList{public static ModList get(){return new ModList();}public boolean isLoaded(String s){return true;}}',
'net/minecraft/world/entity/Entity.java':'''package net.minecraft.world.entity; public class Entity{public float yaw; public float getYRot(){return yaw;}public static net.minecraft.world.phys.Vec3 getInputVector(net.minecraft.world.phys.Vec3 v,float amount,float yaw){throw new AssertionError("not extracted");}}''',
'net/minecraft/client/player/LocalPlayer.java':'package net.minecraft.client.player; public class LocalPlayer extends net.minecraft.world.entity.Entity{}',
'net/minecraft/client/Minecraft.java':'package net.minecraft.client; public class Minecraft{public net.minecraft.client.player.LocalPlayer player=new net.minecraft.client.player.LocalPlayer();}',
'yesman/epicfight/client/world/capabilites/entitypatch/player/LocalPlayerPatch.java':'package yesman.epicfight.client.world.capabilites.entitypatch.player; public class LocalPlayerPatch{}',
'yesman/epicfight/world/capabilities/EpicFightCapabilities.java':'''package yesman.epicfight.world.capabilities; import java.util.Optional; public class EpicFightCapabilities{public static <T> Optional<T> getUnparameterizedEntityPatch(net.minecraft.world.entity.Entity e,Class<T> c){return Optional.of(c.cast(new yesman.epicfight.client.world.capabilites.entitypatch.player.LocalPlayerPatch()));}}''',
'yesman/epicfight/config/ClientConfig.java':'package yesman.epicfight.config; public class ClientConfig{public static boolean lockOnSnapping;}',
'yesman/epicfight/api/client/input/action/InputAction.java':'package yesman.epicfight.api.client.input.action; public interface InputAction{}',
'yesman/epicfight/api/client/input/action/EpicFightInputAction.java':'package yesman.epicfight.api.client.input.action; public class EpicFightInputAction implements InputAction{public static final EpicFightInputAction LOCK_ON_SHIFT_FREELY=new EpicFightInputAction();}',
'yesman/epicfight/api/client/input/InputManager.java':'package yesman.epicfight.api.client.input; public class InputManager{public static boolean free; public static boolean isActionActive(yesman.epicfight.api.client.input.action.InputAction a){return free;}}',
'yesman/epicfight/api/client/camera/EpicFightCameraAPI.java':'''package yesman.epicfight.api.client.camera;
public class EpicFightCameraAPI{
 private static final EpicFightCameraAPI INSTANCE=new EpicFightCameraAPI();
 public net.minecraft.client.Minecraft minecraft=new net.minecraft.client.Minecraft();
 public boolean tps,lockingOnTarget; public float cameraXRot,cameraYRot; public double accumulatedX; public int quickShiftDelay,switches;
 public static EpicFightCameraAPI getInstance(){return INSTANCE;}
 public boolean isTPSMode(){return tps;}public void setCameraRotations(float p,float y,boolean sync){cameraXRot=p;cameraYRot=y;}
 public boolean setNextLockOnTarget(int direction,boolean a,boolean b){switches++;return true;}
 public boolean turnCamera(double y,double p){throw new AssertionError("not extracted");}
 public net.minecraft.world.phys.Vec3 getRelativeMove(net.minecraft.world.phys.Vec3 v,float amount){throw new AssertionError("not extracted");}
}'''
}
for name,content in files.items():
 p=src/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(content)
cp=';'.join([str(classes),*d['compileClasspath'],*dev['classpath']]);java=r/'.tools/java/jdk-21.0.12.1+1/bin';runs=[]
sources=[*src.rglob('*.java'),*sorted((c/'tests/host-look').glob('*.java')),c/'neoforge/src/client/java/dev/goldcraft/client/HostEpicCamera.java']
for name,exe,arguments in [('compile','javac.exe',['--release','21','-proc:none','-encoding','UTF-8','-cp',cp,'-d',classes,*sources]),('extract','java.exe',['-cp',cp,'ExtractEpicCamera',epic,mc,classes]),('regression','java.exe',['-cp',cp,'dev.goldcraft.client.EpicCameraProbe'])]:
 a=out/(name+'.args');a.write_text('\n'.join('"'+str(v).replace('\\','/')+'"' for v in arguments))
 p=subprocess.run([str(java/exe),'@'+str(a)],cwd=out,capture_output=True,timeout=60);(out/(name+'.log')).write_bytes(p.stdout+p.stderr);runs.append({'name':name,'exitCode':p.returncode});print(name,p.returncode,(p.stdout+p.stderr).decode(errors='replace')[-4000:])
 if p.returncode:break
(out/'epic-result.json').write_text(json.dumps({'runs':runs,'epicSha256':hashlib.sha256(epic.read_bytes()).hexdigest(),'boundary':'Extracted exact Epic camera bytecode, exact Minecraft vector math, candidate camera MethodHandles; CPU state adapters. Indicator signatures checked; renderer and FML/live visual pending.'},indent=2)+'\n')
if runs[-1]['exitCode']: raise RuntimeError('Epic probe failed')
