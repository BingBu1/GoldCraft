"""Compare GoldCraft capture against exact local NightFall3.4.0 producer methods.

The handler, fixed70 projection, enum, constructors, tick and shader process are
read from the pinned user JAR at execution time. Only System.nanoTime is redirected
to a deterministic CPU clock. Authored CPU adapters replace game/GL plumbing.
Writes analysis only; no games, FML, GL, deployment or protocol version edits.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=source)
    parser.add_argument('--output', type=Path, default=Path('analysis/bw-contrast-regression'))
    args = parser.parse_args()
    root = args.workspace.resolve()
    raw_output = root / args.output
    for path in [raw_output, *raw_output.parents]:
        if path.is_symlink() or path.is_junction():
            raise ValueError('Refusing redirected output')
    output = raw_output.resolve()
    if not output.is_relative_to(root / 'analysis'):
        raise ValueError('Output must remain in workspace analysis')
    output.mkdir(parents=True, exist_ok=True)
    classes, generated = output / 'classes', output / 'src'
    classes.mkdir(exist_ok=True)
    generated.mkdir(exist_ok=True)
    services = classes / 'META-INF/services'
    services.mkdir(parents=True, exist_ok=True)
    (services / 'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
    (services / 'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
    (classes / 'bw-framebuffer.mixins.json').write_text(json.dumps({
        'required': True, 'package': 'dev.goldcraft.client.mixin', 'compatibilityLevel': 'JAVA_21',
        'client': ['FramebufferClearColorAccessor'], 'injectors': {'defaultRequire': 1}}))
    runtime = json.loads((root / 'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
    cache = root / '.tools/gradle-cache/caches'
    remapper = next((cache / 'modules-2/files-2.1/net.fabricmc/tiny-remapper/0.10.3').glob('*/*.jar'))
    mappingio = next((cache / 'modules-2/files-2.1/net.fabricmc/mapping-io/0.6.1').glob('*/*.jar'))
    mappings = cache / 'fabric-loom/1.21.1/loom.mappings.1_21_1.layered+hash.1998749324-v2-neoforge-21.1.256/mappings-mojang-migrated.tiny'
    mojang = cache / 'fabric-loom/1.21.1/neoforge/21.1.256/minecraft-merged-mojang-at-patched.jar'
    named_game = next(Path(p) for p in runtime['classpath'] if 'minecraft-merged-' in Path(p).name and Path(p).suffix == '.jar')
    jar = root / 'sandbox/neoforge-cs-client-b/mods/NightFall-1.21.1-Neoforge-3.4.0.jar'
    if digest(jar) != 'ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c':
        raise ValueError('Unverified NightFall producer JAR')
    resource_hashes = {
        'black_white_simple.fsh': '89432ec2c4e4565898eeece0d70f1a1d1999ce59790be13d946ebf16825d3abe',
        'black_white_simple.json': 'e398b28b123ee73a18ea3d8838ddb1d12f0988536b431325f6cf476bdf5b9912'}
    with zipfile.ZipFile(jar) as archive:
        for name, expected in resource_hashes.items():
            if hashlib.sha256(archive.read('assets/efn/shaders/program/' + name)).hexdigest() != expected:
                raise ValueError('Unverified shader resource ' + name)
    files = {
        'probe/Clock.java': 'package probe; public final class Clock {public static long now=1000000000L;public static int reads;public static long nanoTime(){reads++;return now;}}',
        'net/minecraft/world/phys/Vec3.java': 'package net.minecraft.world.phys; public class Vec3 {public final double x,y,z; public Vec3(double x,double y,double z){this.x=x;this.y=y;this.z=z;}}',
        'net/minecraft/resources/ResourceLocation.java': 'package net.minecraft.resources; public record ResourceLocation(String namespace,String path){}',
        'net/minecraft/server/packs/resources/ResourceManager.java': 'package net.minecraft.server.packs.resources; public interface ResourceManager{}',
        'net/minecraft/client/multiplayer/ClientLevel.java': 'package net.minecraft.client.multiplayer; public class ClientLevel{}',
        'net/minecraft/client/Camera.java': '''package net.minecraft.client; import net.minecraft.world.phys.Vec3;
public class Camera {public Vec3 position=new Vec3(0,0,0);public float yaw,pitch;
public Vec3 getPosition(){return position;}public float getYRot(){return yaw;}public float getXRot(){return pitch;}}''',
        'com/mojang/blaze3d/platform/Window.java': 'package com.mojang.blaze3d.platform; public class Window{public int width=1920,height=1080;public int getWidth(){return width;}public int getHeight(){return height;}}',
        'net/minecraft/client/renderer/GameRenderer.java': 'package net.minecraft.client.renderer; public class GameRenderer{public final net.minecraft.client.Camera camera=new net.minecraft.client.Camera();public net.minecraft.client.Camera getMainCamera(){return camera;}}',
        'net/minecraft/client/Minecraft.java': '''package net.minecraft.client; public class Minecraft {
static final Minecraft INSTANCE=new Minecraft(); public net.minecraft.client.multiplayer.ClientLevel level=new net.minecraft.client.multiplayer.ClientLevel();
public final net.minecraft.client.renderer.GameRenderer gameRenderer=new net.minecraft.client.renderer.GameRenderer();
public final com.mojang.blaze3d.platform.Window window=new com.mojang.blaze3d.platform.Window();
public final com.mojang.blaze3d.pipeline.RenderTarget target=new com.mojang.blaze3d.pipeline.RenderTarget();
public static Minecraft getInstance(){return INSTANCE;}public com.mojang.blaze3d.platform.Window getWindow(){return window;}
public com.mojang.blaze3d.pipeline.RenderTarget getMainRenderTarget(){return target;}}''',
        'com/mojang/blaze3d/pipeline/RenderTarget.java': '''package com.mojang.blaze3d.pipeline; public class RenderTarget {
public int width=1920,height=1080;public final float[] clearChannels={.1f,.2f,.3f,.4f};
public void unbindWrite(){}public void unbindRead(){}public int getColorTextureId(){return 1;}}''',
        'com/mojang/blaze3d/systems/RenderSystem.java': 'package com.mojang.blaze3d.systems; public class RenderSystem{public static void viewport(int x,int y,int w,int h){}}',
        'com/mojang/blaze3d/shaders/AbstractUniform.java': '''package com.mojang.blaze3d.shaders; public class AbstractUniform {
public float[] values;public int writes;public void set(float a){values=new float[]{a};writes++;}public void set(float a,float b){values=new float[]{a,b};writes++;}
public void set(org.joml.Matrix4f value){writes++;}}''',
        'net/minecraft/client/renderer/EffectInstance.java': '''package net.minecraft.client.renderer; import java.util.*;import com.mojang.blaze3d.shaders.AbstractUniform;
public class EffectInstance {public final Map<String,AbstractUniform> uniforms=new LinkedHashMap<>();public int applies;
public AbstractUniform safeGetUniform(String name){return uniforms.computeIfAbsent(name,key->new AbstractUniform());}
public void setSampler(String name,java.util.function.IntSupplier sampler){}public void apply(){applies++;}public void clear(){}}''',
        'com/guhao/vix/util/OjangUtils.java': 'package com.guhao.vix.util; public class OjangUtils{public static net.minecraft.resources.ResourceLocation newRL(String a,String b){return new net.minecraft.resources.ResourceLocation(a,b);}}',
        'com/guhao/vix/client/pipeline/PostEffectPipelines.java': '''package com.guhao.vix.client.pipeline; public class PostEffectPipelines {
public static org.joml.Matrix4f shaderOrthoMatrix=new org.joml.Matrix4f();public static class Pipeline{public int priority;public void PostEffectHandler(){}}}''',
        'com/guhao/vix/client/screeneffect/ScreenEffectBase.java': '''package com.guhao.vix.client.screeneffect; import net.minecraft.resources.ResourceLocation;import net.minecraft.world.phys.Vec3;
public class ScreenEffectBase{public Vec3 pos;public int age,lifetime;public ScreenEffectBase(ResourceLocation id,Vec3 p){pos=p;}
public static class SE_Pipeline<T extends ScreenEffectBase> extends com.guhao.vix.client.pipeline.PostEffectPipelines.Pipeline {
public ScreenEffectBase effect;public SE_Pipeline(ResourceLocation id,ScreenEffectBase e){effect=e;}}}''',
        'net/neoforged/neoforge/common/ModConfigSpec.java': 'package net.neoforged.neoforge.common; public class ModConfigSpec{public static class BooleanValue {public boolean value=true;public Object get(){return value;}}}',
        'com/hm/efn/EFNClientConfig.java': 'package com.hm.efn; public class EFNClientConfig{public static final net.neoforged.neoforge.common.ModConfigSpec.BooleanValue IMPACT_FRAME=new net.neoforged.neoforge.common.ModConfigSpec.BooleanValue();}',
        'com/guhao/vix/client/targets/TargetManager.java': 'package com.guhao.vix.client.targets; public class TargetManager{public static final com.mojang.blaze3d.pipeline.RenderTarget target=new com.mojang.blaze3d.pipeline.RenderTarget();public static com.mojang.blaze3d.pipeline.RenderTarget getTarget(net.minecraft.resources.ResourceLocation id){return target;}}',
        'com/guhao/vix/client/shaderpasses/PostPassBase.java': '''package com.guhao.vix.client.shaderpasses; import com.mojang.blaze3d.pipeline.RenderTarget;
public class PostPassBase {public net.minecraft.client.renderer.EffectInstance effect=new net.minecraft.client.renderer.EffectInstance();public int draws;
public PostPassBase(String name,net.minecraft.server.packs.resources.ResourceManager resources){}
public void prevProcess(RenderTarget a,RenderTarget b){}public void pushVertex(RenderTarget a,RenderTarget b){draws++;}
public void process(RenderTarget a,RenderTarget b){draws++;}}''',
        'com/hm/efn/registries/PostPasses.java': '''package com.hm.efn.registries; public class PostPasses{
public static com.guhao.vix.client.shaderpasses.PostPassBase blit=new com.guhao.vix.client.shaderpasses.PostPassBase("blit",null);
public static com.hm.efn.client.shaderpasses.BlackWhiteContrastSimple black_white_contrast;
static{try{black_white_contrast=new com.hm.efn.client.shaderpasses.BlackWhiteContrastSimple("black_white_simple",null);}catch(Exception e){throw new ExceptionInInitializerError(e);}}}''',
    }
    for name, content in files.items():
        path = generated / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
    java = root / '.tools/java/jdk-21.0.12.1+1/bin'
    build_classes = root / 'neoforge/build/classes/java/main'
    production_cp = os.pathsep.join([str(build_classes), *runtime['classpath']])
    cp = os.pathsep.join([str(classes), str(jar), str(remapper), str(mappingio), *runtime['classpath']])
    plan = source / 'neoforge/src/main/java/dev/goldcraft/bridge/PostEffectPlan.java'
    capture = source / 'neoforge/src/client/java/dev/goldcraft/client/VixPostCapture.java'
    helper = source / 'neoforge/src/client/java/dev/goldcraft/client/BlackWhiteImpactCapture.java'
    accessor = source / 'neoforge/src/client/java/dev/goldcraft/client/mixin/FramebufferClearColorAccessor.java'
    tests = sorted((source / 'tests/bw-contrast').glob('*.java'))
    services_sources = [root / 'tests/nightfall-blade' / n for n in ('BladeMixinService.java', 'BladeProperties.java')]
    bridge = [root / 'neoforge/src/main/java/dev/goldcraft/bridge' / n for n in ('Wire.java', 'ParticleSnapshot.java')]
    runs = []

    def run(name, executable, arguments):
        argfile = output / (name + '.args')
        argfile.write_text('\n'.join('"' + str(v).replace('\\', '\\\\').replace('"', '\\"') + '"' for v in arguments), encoding='utf-8')
        result = subprocess.run([str(java / executable), '@' + str(argfile)], cwd=output, capture_output=True, timeout=90)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        runs.append({'name': name, 'exitCode': result.returncode})
        if result.returncode:
            raise RuntimeError(f'{name} failed; inspect {output / (name + ".log")}')
        print('PASS ' + name)

    try:
        run('compile-production', 'javac.exe', ['--release', '21', '-proc:none', '-encoding', 'UTF-8', '-cp', production_cp,
            '-d', output / 'production-classes', plan, helper, capture, accessor])
        run('compile-fixtures', 'javac.exe', ['--release', '21', '-proc:none', '-encoding', 'UTF-8', '-cp', cp,
            '-d', classes, *sorted(generated.rglob('*.java')), *tests, plan, helper, accessor, *bridge, *services_sources])
        run('transform-framebuffer', 'java.exe', ['-Dlog4j.configurationFile=none', '-cp', cp, 'TransformFramebuffer', output / 'transformed-framebuffer.class'])
        remap_input, remapped = output / 'remap-input', output / 'remapped'
        accessor_class = Path('dev/goldcraft/client/mixin/FramebufferClearColorAccessor.class')
        (remap_input / accessor_class).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(classes / accessor_class, remap_input / accessor_class)
        run('remap-framebuffer', 'java.exe', ['-cp', cp, 'RemapFramebuffer', mappings, remap_input, named_game, remapped])
        mojang_cp = os.pathsep.join([str(remapped), str(mojang), cp])
        run('transform-framebuffer-mojang', 'java.exe', ['-Dlog4j.configurationFile=none', '-cp', mojang_cp,
            'TransformFramebuffer', output / 'transformed-rendertarget.class', 'mojang'])
        run('prepare-producer', 'java.exe', ['-cp', cp, 'PrepareProducer', jar, classes])
        run('regression', 'java.exe', ['-cp', cp, 'dev.goldcraft.client.BwContrastRegression'])
        run('interop', 'java.exe', ['-cp', cp, 'BwContrastInteropMain', output / 'java-interop.bin'])
        print((output / 'regression.log').read_text(encoding='utf-8').strip())
    finally:
        inputs = [jar, plan, capture, helper, accessor, Path(__file__), *tests, *bridge, *services_sources, remapper, mappingio, mappings, named_game, mojang]
        (output / 'result.json').write_text(json.dumps({'runs': runs, 'resources': resource_hashes,
            'inputs': {str(p): digest(p) for p in inputs},
            'scope': 'exact producer constructors/tick/projection/uniform writes; one declared deterministic clock redirect; authored CPU game/GL adapters; no FML/live pixels/FPS'}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
