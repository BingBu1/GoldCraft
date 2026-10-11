"""Verify the pinned NightFall blade Mixin and unchanged production vertex conversion.

Uses existing workspace Java21, runtime dependencies, and the user's local pinned
NightFall JAR. Writes only analysis output. Actual render/tick methods are extracted
at execution time; no third-party method body is distributed in these fixtures.
This runs Sponge and CPU adapters, not a game/FML/GL session or a visual/FPS test.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    source = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=source,
                        help='Dependency workspace; override only to test an isolated source candidate')
    parser.add_argument('--output', type=Path, default=Path('analysis/nightfall-blade'))
    args = parser.parse_args()
    root = args.workspace.resolve()
    output = (root / args.output).resolve()
    if not output.is_relative_to(root / 'analysis'):
        raise ValueError('Output must remain inside workspace analysis')
    for path in [output, *output.parents]:
        if path.is_symlink() or path.is_junction():
            raise ValueError('Refusing redirected output')
        if path == root:
            break
    output.mkdir(parents=True, exist_ok=True)
    classes = output / 'classes'
    generated = output / 'src'
    classes.mkdir(exist_ok=True)
    generated.mkdir(exist_ok=True)
    runtime = json.loads((root / 'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
    jar = root / 'sandbox/neoforge-cs-client-b/mods/NightFall-1.21.1-Neoforge-3.4.0.jar'
    expected = 'ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c'
    if digest(jar) != expected:
        raise ValueError('NightFall producer differs from the verified 3.4.0 JAR')
    extras = [Path(p) for p in runtime['classpath'] if Path(p).name == 'mixinextras-neoforge-0.5.5.jar']
    if len(extras) != 1 or not extras[0].is_file():
        raise ValueError('Expected existing runtime MixinExtras 0.5.5 dependency')
    java = root / '.tools/java/jdk-21.0.12.1+1/bin'
    build_classes = root / 'neoforge/build/classes/java/main'
    mixin = source / 'neoforge/src/client/java/dev/goldcraft/client/mixin/NightfallBladeGeometryMixin.java'
    capture = source / 'neoforge/src/client/java/dev/goldcraft/client/VixPostCapture.java'
    converter = source / 'neoforge/src/client/java/dev/goldcraft/client/EffectGeometry.java'
    if not converter.exists():
        converter = root / 'neoforge/src/client/java/dev/goldcraft/client/EffectGeometry.java'
    files = {
        'net/minecraft/world/phys/Vec3.java': 'package net.minecraft.world.phys; public record Vec3(double x,double y,double z){}',
        'net/minecraft/client/Camera.java': 'package net.minecraft.client; public class Camera{public net.minecraft.world.phys.Vec3 getPosition(){return new net.minecraft.world.phys.Vec3(0,0,0);}}',
        'com/mojang/blaze3d/vertex/VertexConsumer.java': 'package com.mojang.blaze3d.vertex; public interface VertexConsumer {VertexConsumer addVertex(float x,float y,float z);VertexConsumer setUv(float u,float v);VertexConsumer setColor(float r,float g,float b,float a);VertexConsumer setLight(int light);}',
        'com/guhao/vix/client/pipeline/PostEffectPipelines.java': 'package com.guhao.vix.client.pipeline; public class PostEffectPipelines{public static boolean isActive(){return true;}}',
        'com/hm/efn/client/render/custom/BloomParticleRenderType.java': 'package com.hm.efn.client.render.custom; public class BloomParticleRenderType{public void callPipeline(){}}',
        'com/hm/efn/client/particle/DMC/JCBladeTrail.java': '''package com.hm.efn.client.particle.DMC;
public class JCBladeTrail {
 public static com.hm.efn.client.render.custom.BloomParticleRenderType renderType=new com.hm.efn.client.render.custom.BloomParticleRenderType();
 public double X=1,Y=2,Z=3,xd=1,yd=0,zd=0; public float timeOffset=.75f,rCol=.55f,gCol=.6902f,bCol=1,alpha=.8f;public int age,lifetime=9;public boolean removed;
 public void remove(){removed=true;}public void tick(){throw new AssertionError("UNPATCHED");}
 public void render(com.mojang.blaze3d.vertex.VertexConsumer consumer,net.minecraft.client.Camera camera,float partial){throw new AssertionError("UNPATCHED");}
}'''
    }
    # Reuse the existing self-authored generic ASM extractor, selecting this producer's methods.
    extractor = (root / 'tools/Test-TrailProducer.py').read_text(encoding='utf-8').split("'ExtractMethods.java':'''", 1)[1].split("'''", 1)[0]
    files['ExtractMethods.java'] = extractor.replace('List.of("render","setupPoseStack")', 'List.of("render","tick")')
    # Execute the exact candidate helper with only its active marker/counter adapted,
    # avoiding all Minecraft/GL initialization. Compile the full class separately below.
    original = capture.read_text(encoding='utf-8-sig')
    method = '    public static boolean rejectUndefinedNightfallBlade' + original.split('    public static boolean rejectUndefinedNightfallBlade', 1)[1].split('\n    }', 1)[0] + '\n    }'
    files['dev/goldcraft/client/VixPostCapture.java'] = 'package dev.goldcraft.client; public final class VixPostCapture {public static Object active;public static long undefinedNightfallBlades;\n' + method + '\n}\n'
    for name, text in files.items():
        path = generated / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
    services = classes / 'META-INF/services'
    services.mkdir(parents=True, exist_ok=True)
    (services / 'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
    (services / 'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
    (classes / 'blade-only.mixins.json').write_text(json.dumps({
        'required': True, 'package': 'dev.goldcraft.client.mixin', 'compatibilityLevel': 'JAVA_21',
        'client': ['NightfallBladeGeometryMixin'], 'injectors': {'defaultRequire': 1}}))
    production_cp = os.pathsep.join([str(build_classes), *runtime['classpath']])
    cp = os.pathsep.join([str(classes), str(jar), production_cp])
    runs = []

    def run(name, executable, arguments):
        argfile = output / (name + '.args')
        argfile.write_text('\n'.join('"' + str(v).replace('\\', '\\\\').replace('"', '\\"') + '"' for v in arguments), encoding='utf-8')
        result = subprocess.run([str(java / executable), '@' + str(argfile)], cwd=output, capture_output=True, timeout=90)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        runs.append({'name': name, 'exitCode': result.returncode})
        if result.returncode:
            raise RuntimeError(f'{name} failed; inspect {output / (name + ".log")}')
        print(f'PASS {name}')

    try:
        run('compile-production', 'javac.exe', ['--release', '21', '-proc:none', '-encoding', 'UTF-8', '-cp', production_cp,
                                              '-d', output / 'production-classes', capture, mixin])
        run('compile-fixtures', 'javac.exe', ['--release', '21', '-proc:none', '-encoding', 'UTF-8', '-cp', cp, '-d', classes,
                                            *sorted(generated.rglob('*.java')), *sorted((source / 'tests/nightfall-blade').glob('*.java')), mixin, converter])
        run('extract-original', 'java.exe', ['-cp', cp, 'ExtractMethods', jar, 'com/hm/efn/client/particle/DMC/JCBladeTrail', classes])
        run('transform', 'java.exe', ['-Dlog4j.configurationFile=none', '-cp', cp, 'probe.TransformBlade', jar, output / 'transformed-blade.class', classes])
        run('regression', 'java.exe', ['-cp', cp, 'dev.goldcraft.client.BladeRegression'])
        print((output / 'regression.log').read_text(encoding='utf-8').strip())
    finally:
        inputs = [jar, extras[0], capture, mixin, converter, Path(__file__), *sorted((source / 'tests/nightfall-blade').glob('*.java'))]
        (output / 'result.json').write_text(json.dumps({'runs': runs, 'inputs': {str(p): digest(p) for p in inputs},
            'scope': 'actual Sponge transform / exact producer bytecode / production EffectGeometry; CPU camera, sink, pipeline and active-marker adapters; no game/FML/GL'}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
