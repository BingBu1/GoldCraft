"""Exercise pinned Epic Fight trail render bytecode through CPU adapters and a selected core JAR.

Only generated analysis files are written. No game, FML, Mixin, GL context, network,
or runtime process is started. The two producer methods are copied from the user's
local pinned JAR at execution time; no third-party method body is distributed here.
"""
from pathlib import Path
import zipfile, subprocess, hashlib, argparse, json
r=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description="Execute pinned Epic Fight render bytecode through local CPU adapters and chosen GoldCraft core conversion")
parser.add_argument('--core', type=Path, default=Path('neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar'))
parser.add_argument('--expect', choices=['legacy','finite'], default='finite')
parser.add_argument('--output',type=Path,default=Path('analysis/vix-post/producer-repro-candidate'))
a=parser.parse_args(); gold=(r/a.core).resolve(); o=(r/a.output).resolve()
if not o.is_relative_to(r/'analysis'): raise ValueError('Output must remain in analysis')
s=o/'src';c=o/'classes';s.mkdir(parents=True,exist_ok=True);c.mkdir(exist_ok=True)
files={
'net/minecraft/world/phys/Vec3.java':'''package net.minecraft.world.phys; public class Vec3 { public final double x,y,z; public Vec3(double x,double y,double z){this.x=x;this.y=y;this.z=z;} public double x(){return x;} public double y(){return y;} public double z(){return z;} }''',
'net/minecraft/client/Camera.java':'''package net.minecraft.client; import net.minecraft.world.phys.Vec3; public class Camera { public Vec3 getPosition(){return new Vec3(0,0,0);} }''',
'net/minecraft/util/Mth.java':'''package net.minecraft.util; public class Mth { public static float clamp(float a,float min,float max){return a<min?min:Math.min(a,max);} }''',
'com/mojang/blaze3d/vertex/PoseStack.java':'''package com.mojang.blaze3d.vertex; import org.joml.Matrix4f; public class PoseStack { final Pose pose=new Pose(); public Pose last(){return pose;} public void translate(float x,float y,float z){pose.value.translate(x,y,z);} public static class Pose {final Matrix4f value=new Matrix4f();public Matrix4f pose(){return value;} } }''',
'com/mojang/blaze3d/vertex/VertexConsumer.java':'''package com.mojang.blaze3d.vertex; public interface VertexConsumer {VertexConsumer addVertex(float x,float y,float z);VertexConsumer setUv(float u,float v);VertexConsumer setColor(float r,float g,float b,float a);VertexConsumer setLight(int light);}''',
'yesman/epicfight/api/client/animation/property/TrailInfo.java':'''package yesman.epicfight.api.client.animation.property; public record TrailInfo(int interpolateCount,int trailLifetime,float fadeTime){ public static boolean isValidTime(float time){return time>=0;} }''',
'yesman/epicfight/client/particle/AbstractTrailParticle.java':'''package yesman.epicfight.client.particle; import java.util.*;import com.mojang.blaze3d.vertex.*;import net.minecraft.client.Camera;import net.minecraft.world.phys.Vec3;import yesman.epicfight.api.client.animation.property.TrailInfo; public class AbstractTrailParticle {public List<TrailEdge> trailEdges=new ArrayList<>();public TrailInfo trailInfo=new TrailInfo(4,4,-1); public float startEdgeCorrection=0,rCol=1,gCol=1,bCol=1,alpha=1;public boolean shouldRemove=false;public int age=1,lifetime=4; public int getLightColor(float partial){return 15728880;}public void setupPoseStack(PoseStack pose,Camera camera,float partial){throw new AssertionError("UNPATCHED");}public void render(VertexConsumer vertices,Camera camera,float partial){throw new AssertionError("UNPATCHED");}public static class TrailEdge{public Vec3 start,end;public int lifetime;public TrailEdge(Vec3 start,Vec3 end,int life){this.start=start;this.end=end;this.lifetime=life;}}}''',

'ExtractMethods.java':'''
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import java.security.*;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;
public class ExtractMethods {
    static byte[] canonical(MethodNode method) {
        var w=new ClassWriter(0);
        w.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"MethodIdentity",null,"java/lang/Object",null);
        method.accept(w);w.visitEnd();return w.toByteArray();
    }
    public static void main(String[] a)throws Exception {
        try(var z=new ZipFile(a[0])) {
            String name=a[1];var original=new ClassNode();
            new ClassReader(z.getInputStream(z.getEntry(name+".class"))).accept(original,0);
            Path target=Path.of(a[2],name+".class");var stub=new ClassNode();
            new ClassReader(Files.readAllBytes(target)).accept(stub,0);
            var identities=new LinkedHashMap<String,byte[]>();
            for(String method:List.of("render","setupPoseStack")) {
                var chosen=original.methods.stream().filter(m->m.name.equals(method)).findFirst().orElseThrow();
                identities.put(method,canonical(chosen));
                stub.methods.removeIf(m->m.name.equals(method));stub.methods.add(chosen);
            }
            var w=new ClassWriter(0);stub.accept(w);Files.write(target,w.toByteArray());
            var actual=new ClassNode();new ClassReader(Files.readAllBytes(target)).accept(actual,0);
            for(var entry:identities.entrySet()) {
                var m=actual.methods.stream().filter(v->v.name.equals(entry.getKey())).findFirst().orElseThrow();
                var bytes=canonical(m);
                if(!Arrays.equals(entry.getValue(),bytes))throw new AssertionError("Method changed: "+m.name);
                System.out.println("METHOD_IDENTICAL "+name+"."+m.name+m.desc+" instructions="+m.instructions.size()+
                    " sha256="+HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes)));
            }
        }
    }
}
''',

'dev/goldcraft/client/ProducerRepro.java':'''
package dev.goldcraft.client;
import java.nio.*;
import java.util.*;
import com.mojang.blaze3d.vertex.*;
import net.minecraft.client.Camera;
import net.minecraft.world.phys.Vec3;
import yesman.epicfight.client.particle.AbstractTrailParticle;
import yesman.epicfight.api.physics.bezier.CubicBezierCurve;
import org.joml.Matrix4f;
public class ProducerRepro {
    static int checks,accepted,rejected;
    static class Collector implements VertexConsumer {
        ByteBuffer b=ByteBuffer.allocate(65536).order(ByteOrder.nativeOrder());int count,at;
        public VertexConsumer addVertex(float x,float y,float z){at=count++*28;b.putFloat(at,x).putFloat(at+4,y).putFloat(at+8,z);return this;}
        public VertexConsumer setUv(float u,float v){b.putFloat(at+12,u).putFloat(at+16,v);return this;}
        public VertexConsumer setColor(float r,float g,float bl,float a){b.put(at+20,(byte)(r*255)).put(at+21,(byte)(g*255)).put(at+22,(byte)(bl*255)).put(at+23,(byte)(a*255));return this;}
        public VertexConsumer setLight(int light){b.putInt(at+24,light);return this;}
    }
    static void check(boolean condition,String message){checks++;if(!condition)throw new AssertionError(message);}
    static void run(String name,int edges,float partial,float correction,boolean expect,int expectedVertices,float expectedThird) {
        var p=new AbstractTrailParticle();p.startEdgeCorrection=correction;
        for(int i=0;i<edges;i++)p.trailEdges.add(new AbstractTrailParticle.TrailEdge(new Vec3(i,0,0),new Vec3(i,1,0),4));
        var v=new Collector();p.render(v,new Camera(),partial);
        check(v.count==expectedVertices,name+" vertex count");
        float first=v.b.getFloat(12),third=v.b.getFloat(68);
        check(Float.isNaN(expectedThird)?Float.isNaN(third):Math.abs(third-expectedThird)<.01f,name+" third UV");
        boolean ok=false;String detail="";
        try {
            var result=EffectGeometry.convert(v.b.limit(v.count*28),v.count,new EffectGeometry.Layout(28,0,20,12,24),
                EffectGeometry.Mode.QUADS,null,0,new Matrix4f(),0,0,0,false,65536);
            ok=true;accepted++;
            var out=ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN);
            check(result.length==v.count*24,name+" output size");
            for(int i=0;i<v.count;i++)for(int component=0;component<5;component++) {
                int source=Float.floatToRawIntBits(v.b.getFloat(i*28+component*4));
                int value=Float.floatToRawIntBits(out.getFloat(i*24+component*4));
                check(source==value,name+" exact unchanged position/UV at "+i+"/"+component);
            }
            detail="accepted="+result.length;
        }catch(IllegalArgumentException e) {
            rejected++;var line=Arrays.stream(e.getStackTrace()).filter(t->t.getClassName().endsWith("EffectGeometry")&&t.getMethodName().equals("convert")).findFirst().orElseThrow();
            detail="rejected="+e.getMessage()+" convertLine="+line.getLineNumber();
        }
        check(ok==expect,name+" acceptance expectation");
        System.out.println(name+" edges="+edges+" partial="+partial+" correction="+correction+" vertices="+v.count+" firstU="+first+" thirdU="+third+" "+detail);
    }
    public static void main(String[] args) {
        // The first visible AnimationTrailParticle curve uses this exact installed method with (list,1,3,4).
        var points=CubicBezierCurve.getBezierInterpolatedPoints(List.of(new Vec3(0,0,0),new Vec3(1,0,0),new Vec3(2,0,0),new Vec3(3,0,0)),1,3,4);
        check(points.size()==9,"Actual first-curve point count");
        System.out.println("BEZIER_ACTUAL firstCurvePoints="+points.size());
        boolean finite=args[0].equals("finite");
        run("birth-zero",9,0,0,false,4,Float.NaN);
        run("birth-small",9,.001f,0,finite,4,125.00161f);
        run("birth-valid",9,.01f,0,true,4,12.500012f);
        run("birth-mid",9,.5f,0,true,20,.25f);
        run("stable",17,.001f,0,true,36,.124875136f);
        run("fractional-correction",9,.03f,.2f,finite,4,20.000116f);
        System.out.println("PASS checks="+checks+" fixtures=6 accepted="+accepted+" rejected="+rejected+" expectation="+args[0]);
    }
}
'''

}
for name, source in files.items():
    destination = s / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(source, encoding="utf-8")

epic = r / "sandbox/neoforge-cs-client-b/mods/epic-fight-21.17.3.1-mc1.21.1-neoforge.jar"
if hashlib.sha256(epic.read_bytes()).hexdigest() != "8b882554cf10086398340fbdc741819ee72a801a3adce516c7f4768326a39526":
    raise ValueError("Epic Fight producer JAR differs from the verified version")
with zipfile.ZipFile(epic) as archive:
    name = "yesman/epicfight/api/physics/bezier/CubicBezierCurve.class"
    destination = c / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(archive.read(name))
with zipfile.ZipFile(gold) as archive:
    for name in archive.namelist():
        if name.startswith("dev/goldcraft/client/EffectGeometry") and name.endswith(".class"):
            destination = c / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive.read(name))

asm = r / ".tools/gradle/gradle-8.10.2/lib"
modules = r / ".tools/gradle-cache/caches/modules-2/files-2.1"
joml = next((modules / "org.joml/joml/1.10.5").rglob("*.jar"))
fastutil = next((modules / "it.unimi.dsi/fastutil/8.5.12").rglob("*.jar"))
guava = next((modules / "com.google.guava/guava/32.1.2-jre").rglob("*.jar"))
classpath = ";".join(map(str, [c, joml, fastutil, guava, asm / "asm-9.7.jar", asm / "asm-tree-9.7.jar"]))
java = r / ".tools/java/jdk-21.0.12.1+1/bin"
commands = [
    [str(java / "javac.exe"), "--release", "21", "-cp", classpath, "-d", str(c), *map(str, s.rglob("*.java"))],
    [str(java / "java.exe"), "-cp", classpath, "ExtractMethods", str(epic), "yesman/epicfight/client/particle/AbstractTrailParticle", str(c)],
    [str(java / "java.exe"), "-cp", classpath, "dev.goldcraft.client.ProducerRepro", a.expect],
]
inputs = [Path(__file__).resolve(), epic, gold, joml, fastutil, guava, asm / "asm-9.7.jar", asm / "asm-tree-9.7.jar"]
evidence = {
    "expectation": a.expect,
    "scope": "Exact producer render/setup bytecode with equivalence assertions, actual Bezier helper and JOML; adapter camera/pose/vertices/light and synthetic edge inputs. No game/FML/Mixin/GL execution.",
    "inputs": {str(path.relative_to(r)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs},
    "runs": [],
}
log = []
for command in commands:
    result = subprocess.run(command, cwd=r, capture_output=True, timeout=120)
    message = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    print(message)
    log.append(message)
    evidence["runs"].append({"exitCode": result.returncode, "command": [value.replace(str(r), "WORKSPACE") for value in command]})
    (o / "repro.log").write_text("\n".join(log), encoding="utf-8")
    (o / "evidence.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    if result.returncode:
        raise SystemExit(result.returncode)
(o / "inputs.txt").write_text("\n".join(str(path.relative_to(r)) + " sha256=" + hashlib.sha256(path.read_bytes()).hexdigest() for path in [epic, gold, joml]), encoding="utf-8")
