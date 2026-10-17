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

/** Checks the shipped, Mojang-remapped mixins against the production game JAR. */
public final class ProductionTransform {
    public static void main(String[] args)throws Exception{
        MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.CLIENT);
        Mixins.addConfiguration("controls.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();ctor.setAccessible(true);
        var transformer=(IMixinTransformer)ctor.newInstance();int methods=0;
        try(var jar=new ZipFile(args[0])){
            for(String owner:List.of("net/minecraft/client/Minecraft","net/minecraft/client/MouseHandler","com/mojang/blaze3d/platform/InputConstants")){
                byte[] before=jar.getInputStream(jar.getEntry(owner+".class")).readAllBytes();
                byte[] after=transformer.transformClass(env,owner.replace('/','.'),before);
                if(Arrays.equals(before,after))throw new AssertionError(owner+" not transformed");
                var node=new ClassNode();new ClassReader(after).accept(node,0);int hooks=0;
                for(var m:node.methods){
                    if((m.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(owner,m);methods++;}
                    for(var i:m.instructions)if(i instanceof MethodInsnNode c&&c.owner.equals(owner)&&c.name.contains("goldcraft$"))hooks++;
                }
                if(hooks!=(owner.endsWith("MouseHandler")?4:1))throw new AssertionError("wrong hooks "+owner+":"+hooks);
                Files.write(Path.of(args[1],owner.substring(owner.lastIndexOf('/')+1)+"-production.class"),after);
            }
        }
        System.out.println("PASS production remapped JAR: three real game targets, six required hooks; verified methods="+methods);
    }
}
