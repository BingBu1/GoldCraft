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

/** Apply actual Sponge injection to pinned game and optional AAA bytecode. */
public final class DepthTransform {
    public static void main(String[] args)throws Exception {
        MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.CLIENT);
        Mixins.addConfiguration("depth.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();ctor.setAccessible(true);
        var transformer=(IMixinTransformer)ctor.newInstance();int methods=0,hooks=0,targets=0;
        String framebuffer=args[2].equals("production")?"com/mojang/blaze3d/pipeline/RenderTarget":"net/minecraft/client/gl/Framebuffer";
        var entries=new LinkedHashMap<String,Path>();entries.put(framebuffer,Path.of(args[0]));entries.put("com/mojang/blaze3d/platform/GlStateManager",Path.of(args[0]));
        if(args.length>3)entries.put("mod/chloeprime/aaaparticles/client/render/RenderUtil",Path.of(args[3]));
        for(var entry:entries.entrySet())try(var jar=new ZipFile(entry.getValue().toFile())){
            String owner=entry.getKey();byte[] before=jar.getInputStream(jar.getEntry(owner+".class")).readAllBytes();
            byte[] after=transformer.transformClass(env,owner.replace('/','.'),before);
            if(Arrays.equals(before,after))throw new AssertionError(owner+" was not transformed");
            var node=new ClassNode();new ClassReader(after).accept(node,0);int current=0;
            for(var method:node.methods){
                if((method.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(owner,method);methods++;}
                for(var insn:method.instructions)if(insn instanceof MethodInsnNode call&&call.owner.equals(owner)&&call.name.contains("goldcraft$"))current++;
            }
            int expected=owner.equals(framebuffer)?2:owner.endsWith("RenderUtil")?3:1;
            if(current!=expected)throw new AssertionError(owner+" hooks="+current+" expected="+expected);
            hooks+=current;targets++;Files.write(Path.of(args[1],owner.substring(owner.lastIndexOf('/')+1)+"-"+args[2]+".class"),after);
            if(owner.endsWith("RenderUtil")){
                var renamed=new ClassNode();new ClassReader(after).accept(new ClassRemapper(renamed,new Remapper(){
                    @Override public String map(String name){return name.equals(owner)?"ActualDepthCalls":name.equals("com/mojang/blaze3d/pipeline/RenderTarget")?"ActualDepthCalls$Target":name;}
                }),0);
                Path target=Path.of(args[1],"classes/ActualDepthCalls.class");var adapter=new ClassNode();new ClassReader(Files.readAllBytes(target)).accept(adapter,0);
                adapter.methods.removeIf(m->!m.name.equals("<init>"));
                for(var m:renamed.methods)if(m.name.contains("goldcraft$")||m.name.equals("copySafely")
                    ||m.name.equals("copyDepthSafely")&&m.desc.contains("III"))adapter.methods.add(m);
                var writer=new ClassWriter(ClassWriter.COMPUTE_MAXS);adapter.accept(writer);Files.write(target,writer.toByteArray());
            }
        }
        System.out.println("PASS actual "+args[2]+" Sponge targets="+targets+" hooks="+hooks+" verifiedMethods="+methods);
    }
}
