import java.nio.file.*;
import java.util.*;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;
import org.objectweb.asm.tree.analysis.*;
import org.spongepowered.asm.launch.MixinBootstrap;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.transformer.IMixinTransformer;

/** Applies the real accessor through Sponge to the actual mapped Minecraft runtime target. */
public final class TransformFramebuffer {
    public static void main(String[] args)throws Exception {
        MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.CLIENT);
        Mixins.addConfiguration("bw-framebuffer.mixins.json");
        var constructor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();
        constructor.setAccessible(true);var transformer=(IMixinTransformer)constructor.newInstance();
        boolean mojang=args.length>1&&args[1].equals("mojang");
        String owner=mojang?"com.mojang.blaze3d.pipeline.RenderTarget":"net.minecraft.client.gl.Framebuffer";
        String fieldName=mojang?"clearChannels":"clearColor";byte[] before;
        try(var input=TransformFramebuffer.class.getClassLoader().getResourceAsStream(owner.replace('.','/')+".class")) {
            if(input==null)throw new AssertionError("Missing actual mapped Minecraft target");before=input.readAllBytes();
        }
        var original=new ClassNode();new ClassReader(before).accept(original,0);
        if(original.fields.stream().filter(f->f.name.equals(fieldName)&&f.desc.equals("[F")).count()!=1)
            throw new AssertionError("Actual target field drift");
        byte[] after=transformer.transformClass(env,owner,before);
        if(Arrays.equals(before,after))throw new AssertionError("Accessor not transformed");
        var result=new ClassNode();new ClassReader(after).accept(result,0);
        var getter=result.methods.stream().filter(m->m.name.equals("goldcraft$clearColor")).findFirst().orElseThrow();
        new Analyzer<>(new BasicVerifier()).analyze(result.name,getter);
        int reads=0,returns=0;
        for(var instruction:getter.instructions) {
            if(instruction instanceof FieldInsnNode field) {
                if(field.getOpcode()!=Opcodes.GETFIELD||!field.owner.equals(result.name)||!field.name.equals(fieldName)||!field.desc.equals("[F"))
                    throw new AssertionError("Wrong accessor field");reads++;
            }
            if(instruction.getOpcode()==Opcodes.ARETURN)returns++;
            if(instruction instanceof MethodInsnNode)throw new AssertionError("Unexpected calls in CPU accessor");
        }
        if(reads!=1||returns!=1)throw new AssertionError("Accessor read/return counts");
        Files.write(Path.of(args[0]),after);
        System.out.println("PASS actual Sponge accessor on actual runtime "+owner+"."+fieldName+":[F; one direct CPU field read, no method/GL call");
    }
}
