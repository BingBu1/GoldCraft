package probe;

import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;
import org.objectweb.asm.tree.analysis.*;
import org.spongepowered.asm.launch.MixinBootstrap;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.transformer.IMixinTransformer;

/** Transform the pinned producer without initializing its animation registry. */
public final class ConfigTransform {
    public static void main(String[] args) throws Exception {
        MixinBootstrap.init();
        var env=MixinEnvironment.getCurrentEnvironment();
        env.setSide(MixinEnvironment.Side.valueOf(args[2]));
        Mixins.addConfiguration("config-only.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();
        ctor.setAccessible(true);
        var transformer=(IMixinTransformer)ctor.newInstance();
        String owner="com.hm.efn.gameasset.animations.EFNMurasamaAnimations";
        byte[] before;
        try(var zip=new ZipFile(args[0])) {
            before=zip.getInputStream(zip.getEntry(owner.replace('.','/')+".class")).readAllBytes();
        }
        byte[] after=transformer.transformClass(env,owner,before);
        int flags=ClassReader.SKIP_DEBUG|ClassReader.SKIP_FRAMES;
        var original=new ClassNode();new ClassReader(before).accept(original,flags);
        var patched=new ClassNode();new ClassReader(after).accept(patched,flags);
        var handler=patched.methods.stream().filter(m->m.name.contains("goldcraft$serverVisualDefault")).findFirst().orElseThrow();
        int redirects=0,verified=0;
        for(var method:patched.methods) {
            if((method.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0) {
                new Analyzer<>(new BasicVerifier()).analyze(patched.name,method);verified++;
            }
            for(var insn:method.instructions) if(insn instanceof MethodInsnNode call && call.name.equals(handler.name)) {
                if(!method.name.equals("lambda$static$4") || call.getOpcode()!=Opcodes.INVOKESTATIC)
                    throw new AssertionError("Unexpected redirect site");
                redirects++;
                // Sponge spills/reloads the receiver for the static redirect.
                var load=call.getPrevious();var save=load.getPrevious();
                if(!(load instanceof VarInsnNode l) || !(save instanceof VarInsnNode s)
                    || l.getOpcode()!=Opcodes.ALOAD || s.getOpcode()!=Opcodes.ASTORE || l.var!=s.var
                    || l.var<original.methods.stream().filter(m->m.name.equals(method.name)&&m.desc.equals(method.desc)).findFirst().orElseThrow().maxLocals)
                    throw new AssertionError("Unexpected receiver adapter");
                method.instructions.remove(load);method.instructions.remove(save);
                method.instructions.set(call,new MethodInsnNode(Opcodes.INVOKEVIRTUAL,
                    "net/neoforged/neoforge/common/ModConfigSpec$BooleanValue","get","()Ljava/lang/Object;",false));
            }
        }
        if(redirects!=1)throw new AssertionError("Expected exactly one redirect: "+redirects);
        for(var method:original.methods) {
            var normalized=patched.methods.stream().filter(m->m.name.equals(method.name)&&m.desc.equals(method.desc)).findFirst().orElseThrow();
            normalized.maxStack=method.maxStack;normalized.maxLocals=method.maxLocals;
            if(!Arrays.equals(canonical(method),canonical(normalized))) {
                Path output=Path.of(args[1]);
                for(var entry:Map.of("before",method,"after",normalized).entrySet()) {
                    var printer=new org.objectweb.asm.util.Textifier();
                    entry.getValue().accept(new org.objectweb.asm.util.TraceMethodVisitor(printer));
                    try(var stream=new java.io.PrintWriter(output.resolve(entry.getKey()+".txt").toFile())){printer.print(stream);}
                }
                throw new AssertionError("Unrelated animation bytecode changed: "+method.name);
            }
        }
        // Execute the exact injected handler separately, with explicit side/config adapters.
        var writer=new ClassWriter(ClassWriter.COMPUTE_FRAMES|ClassWriter.COMPUTE_MAXS);
        writer.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"probe/InjectedConfig",null,"java/lang/Object",null);
        handler.access=Opcodes.ACC_PUBLIC|Opcodes.ACC_STATIC;handler.name="apply";
        handler.accept(writer);writer.visitEnd();
        Path dest=Path.of(args[1],"probe/InjectedConfig.class");
        Files.createDirectories(dest.getParent());Files.write(dest,writer.toByteArray());
        System.out.println("PASS "+args[2]+": one real Sponge redirect; all "+original.methods.size()+
            " original methods unchanged after call normalization; "+verified+" methods verified");
    }
    private static byte[] canonical(MethodNode method) {
        var writer=new ClassWriter(0);
        writer.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"Identity",null,"java/lang/Object",null);
        method.accept(writer);writer.visitEnd();return writer.toByteArray();
    }
}
