import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.WeakHashMap;
import org.objectweb.asm.*;
import org.objectweb.asm.commons.ClassRemapper;
import org.objectweb.asm.commons.SimpleRemapper;
import org.objectweb.asm.tree.*;

/** Execute the production cache bytecode with a small layer/factory fixture, without bootstrapping a game. */
public final class MaidCaptureProbe {
    public enum Mode { QUADS, TRIANGLES }
    public static final class Layer {
        private final Mode mode;
        Layer(Mode mode){this.mode=mode;}
        public Mode mode(){return mode;}
    }
    public static final class Triangulate {
        static int calls;
        public static Layer makeTriangulated(Layer source){calls++;return new Layer(Mode.TRIANGLES);}
    }
    private static final class Loader extends ClassLoader {
        Class<?> define(byte[] bytes){return defineClass("CapturedRenderTypes",bytes,0,bytes.length);}
    }
    public static void main(String[] args) throws Exception {
        int vertices=Integer.parseInt(args[0]);
        if(vertices!=3186||vertices%3!=0||vertices%4==0)throw new AssertionError("Captured topology no longer reproduces incomplete QUADS");
        var source=new ClassNode();new ClassReader(Files.readAllBytes(Path.of(args[1]))).accept(source,0);
        var original=source.methods.stream().filter(m->m.name.equals("externalCaptureRenderType")).findFirst().orElseThrow();
        String field="TRIANGULATED_CAPTURE_RENDER_TYPES";
        if(source.fields.stream().noneMatch(f->f.name.equals(field)&&f.desc.equals("Ljava/util/Map;")))throw new AssertionError("Cache field changed");
        var fixture=new ClassNode();fixture.version=Opcodes.V21;fixture.access=Opcodes.ACC_PUBLIC;
        fixture.name=source.name;fixture.superName="java/lang/Object";
        fixture.fields.add(new FieldNode(Opcodes.ACC_PRIVATE|Opcodes.ACC_STATIC,field,"Ljava/util/Map;",null,null));
        original.access=Opcodes.ACC_PUBLIC|Opcodes.ACC_STATIC;fixture.methods.add(original);
        var init=new MethodNode(Opcodes.ACC_STATIC,"<clinit>","()V",null,null);
        init.visitTypeInsn(Opcodes.NEW,"java/util/WeakHashMap");init.visitInsn(Opcodes.DUP);
        init.visitMethodInsn(Opcodes.INVOKESPECIAL,"java/util/WeakHashMap","<init>","()V",false);
        init.visitFieldInsn(Opcodes.PUTSTATIC,source.name,field,"Ljava/util/Map;");init.visitInsn(Opcodes.RETURN);
        fixture.methods.add(init);
        var substitutions=Map.of(source.name,"CapturedRenderTypes",
            "net/minecraft/client/renderer/RenderType","MaidCaptureProbe$Layer",
            "com/mojang/blaze3d/vertex/VertexFormat$Mode","MaidCaptureProbe$Mode",
            "yesman/epicfight/client/renderer/EpicFightRenderTypes","MaidCaptureProbe$Triangulate");
        var writer=new ClassWriter(ClassWriter.COMPUTE_FRAMES|ClassWriter.COMPUTE_MAXS);
        fixture.accept(new ClassRemapper(writer,new SimpleRemapper(substitutions)));
        var cache=new Loader().define(writer.toByteArray()).getMethod(original.name,Layer.class);
        var first=new Layer(Mode.QUADS);var sibling=new Layer(Mode.QUADS);var triangles=new Layer(Mode.TRIANGLES);
        var result=(Layer)cache.invoke(null,first);var second=(Layer)cache.invoke(null,sibling);
        if(result.mode()!=Mode.TRIANGLES||second==result||cache.invoke(null,first)!=result
            ||cache.invoke(null,sibling)!=second||cache.invoke(null,triangles)!=triangles||Triangulate.calls!=2)
            throw new AssertionError("Production cache loses topology, source identity or reuse");
        for(int frame=0;frame<10000;frame++)if(cache.invoke(null,first)!=result)throw new AssertionError("Warm cache changed");
        if(Triangulate.calls!=2)throw new AssertionError("Warm frame allocated another render layer");
        System.out.println("{\"capturedVertices\":3186,\"cacheBytecodeExecuted\":true,\"identityIsolation\":true,\"warmLookups\":10000,\"warmFactoryCalls\":0,\"realMinecraftRenderTypeExecution\":false}");
    }
}
