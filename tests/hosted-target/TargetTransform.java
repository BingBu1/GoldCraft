package probe;

import java.nio.file.*;
import java.util.*;
import java.util.zip.*;
import org.objectweb.asm.*;
import org.objectweb.asm.commons.*;
import org.objectweb.asm.tree.*;
import org.objectweb.asm.tree.analysis.*;
import org.spongepowered.asm.launch.MixinBootstrap;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.transformer.IMixinTransformer;

/** Verify real target injection, then transplant only the actual getter and TACZ helper into observers. */
public final class TargetTransform {
    public static void main(String[] args)throws Exception {
        boolean prod=args[2].equals("production");String owner=prod?"net/minecraft/client/Minecraft":"net/minecraft/client/MinecraftClient";
        String framebuffer=prod?"com/mojang/blaze3d/pipeline/RenderTarget":"net/minecraft/client/gl/Framebuffer";
        Path classes=Path.of(args[1]);MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.CLIENT);Mixins.addConfiguration("hosted-target.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();ctor.setAccessible(true);var transformer=(IMixinTransformer)ctor.newInstance();
        try(var jar=new ZipFile(args[0])){
            byte[] before=jar.getInputStream(jar.getEntry(owner+".class")).readAllBytes();byte[] after=transformer.transformClass(env,owner.replace('/','.'),before);if(Arrays.equals(before,after))throw new AssertionError("not transformed");
            var node=new ClassNode();new ClassReader(after).accept(node,0);int methods=0,hooks=0;
            for(var m:node.methods){if((m.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(owner,m);methods++;}for(var in:m.instructions)if(in instanceof MethodInsnNode call&&call.owner.equals(owner)&&call.name.contains("goldcraft$"))hooks++;}
            if(hooks!=1)throw new AssertionError("hooks="+hooks);
            String getter=prod?"getMainRenderTarget":"getFramebuffer";
            var adapter=new ClassNode();Path path=classes.resolve(owner+".class");new ClassReader(Files.readAllBytes(path)).accept(adapter,0);adapter.methods.removeIf(m->m.name.equals(getter));
            for(var m:node.methods)if(m.name.equals(getter)||m.name.contains("goldcraft$"))adapter.methods.add(m);
            var writer=new ClassWriter(ClassWriter.COMPUTE_MAXS);adapter.accept(writer);Files.write(path,writer.toByteArray());
            System.out.println("PASS actual "+args[2]+" Minecraft target getter: hooks="+hooks+", verifiedMethods="+methods);
        }
        try(var jar=new ZipFile(args[3])){
            String actual="com/tacz/guns/util/RenderHelper",dest="dev/goldcraft/client/ActualScopeCalls";var renamed=new ClassNode();
            new ClassReader(jar.getInputStream(jar.getEntry(actual+".class"))).accept(new ClassRemapper(renamed,new Remapper(){
                @Override public String map(String n){return n.equals(actual)?dest:n.equals("net/minecraft/client/Minecraft")?owner:n.equals("com/mojang/blaze3d/pipeline/RenderTarget")?framebuffer:n;}
                @Override public String mapMethodName(String o,String name,String desc){return o.equals(actual)&&name.equals("enableItemEntityStencilTest")?"enable":!prod&&o.equals("net/minecraft/client/Minecraft")&&name.equals("getMainRenderTarget")?"getFramebuffer":name;}
            }),0);
            var adapter=new ClassNode();Path path=classes.resolve(dest+".class");new ClassReader(Files.readAllBytes(path)).accept(adapter,0);adapter.methods.removeIf(m->m.name.equals("enable"));for(var m:renamed.methods)if(m.name.equals("enable"))adapter.methods.add(m);
            var writer=new ClassWriter(ClassWriter.COMPUTE_MAXS);adapter.accept(writer);Files.write(path,writer.toByteArray());
        }
    }
}
