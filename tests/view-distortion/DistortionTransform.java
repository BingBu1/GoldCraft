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

/** Verify all four real accessor targets in both mapped and shipped GameRenderer. */
public final class DistortionTransform {
    public static void main(String[] args)throws Exception {
        MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.CLIENT);
        Mixins.addConfiguration("distortion.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();ctor.setAccessible(true);
        var transformer=(IMixinTransformer)ctor.newInstance();String owner=args[2].equals("production")?"net/minecraft/client/renderer/GameRenderer":"net/minecraft/client/render/GameRenderer";
        try(var jar=new ZipFile(args[0])){
            byte[] before=jar.getInputStream(jar.getEntry(owner+".class")).readAllBytes();
            byte[] after=transformer.transformClass(env,owner.replace('/','.'),before);if(Arrays.equals(before,after))throw new AssertionError("not transformed");
            var node=new ClassNode();new ClassReader(after).accept(node,0);int methods=0,hooks=0;
            for(var method:node.methods){
                if((method.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(owner,method);methods++;}
                if(method.name.startsWith("goldcraft$")){
                    hooks++;
                    if(method.name.equals("goldcraft$ticks")&&method.instructions.iterator().hasNext()){
                        boolean found=false;for(var insn:method.instructions)if(insn instanceof FieldInsnNode f&&f.owner.equals(owner)&&f.desc.equals("I"))found=true;
                        if(!found)throw new AssertionError("ticks getter not bound");
                    }
                    if(method.name.equals("goldcraft$renderNausea")){
                        boolean found=false;for(var insn:method.instructions)if(insn instanceof MethodInsnNode m&&m.owner.equals(owner))found=true;
                        if(!found)throw new AssertionError("nausea invoker not bound");
                    }
                }
            }
            if(hooks!=4)throw new AssertionError("accessors="+hooks);
            Files.write(Path.of(args[1],"GameRenderer-"+args[2]+".class"),after);
            System.out.println("PASS actual "+args[2]+" Sponge GameRenderer: accessors="+hooks+", verifiedMethods="+methods);
        }
    }
}
