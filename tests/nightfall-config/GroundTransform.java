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

/** Extract the actual producer loop; the world/shape API is a CPU adapter. */
public final class GroundTransform {
    public static void main(String[] args)throws Exception {
        MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.SERVER);
        Mixins.addConfiguration("ground-only.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();ctor.setAccessible(true);
        String owner="com.hm.efn.gameasset.animations.EFNYamatoAnimations";byte[] before;
        try(var zip=new ZipFile(args[0])){before=zip.getInputStream(zip.getEntry(owner.replace('.','/')+".class")).readAllBytes();}
        byte[] after=((IMixinTransformer)ctor.newInstance()).transformClass(env,owner,before);
        var original=new ClassNode();var patched=new ClassNode();int flags=ClassReader.SKIP_DEBUG|ClassReader.SKIP_FRAMES;
        new ClassReader(before).accept(original,flags);new ClassReader(after).accept(patched,flags);
        var handler=patched.methods.stream().filter(m->m.name.contains("goldcraft$ground")).findFirst().orElseThrow();
        var target=patched.methods.stream().filter(m->m.name.equals("lambda$static$3")).findFirst().orElseThrow();
        var old=original.methods.stream().filter(m->m.name.equals(target.name)).findFirst().orElseThrow();
        var output=new ClassWriter(ClassWriter.COMPUTE_FRAMES|ClassWriter.COMPUTE_MAXS);
        output.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"probe/GroundLoops",null,"java/lang/Object",null);
        loop(old,"original",patched.name,handler.name).accept(output);
        loop(target,"patched",patched.name,handler.name).accept(output);
        var copy=new MethodNode(Opcodes.ACC_PUBLIC|Opcodes.ACC_STATIC,handler.name,handler.desc,handler.signature,handler.exceptions.toArray(String[]::new));
        handler.accept(copy);copy.accept(output);
        output.visitEnd();Path dest=Path.of(args[1],"probe/GroundLoops.class");Files.createDirectories(dest.getParent());Files.write(dest,output.toByteArray());
        int redirects=0;
        for(var method:patched.methods){
            if((method.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0)new Analyzer<>(new BasicVerifier()).analyze(patched.name,method);
            for(var instruction:method.instructions)if(instruction instanceof MethodInsnNode call&&call.name.equals(handler.name)){
                if(method!=target)throw new AssertionError("Wrong injection method");redirects++;
                var a=call.getPrevious();var b=a.getPrevious();var c=b.getPrevious();var d=c.getPrevious();
                if(!(a instanceof VarInsnNode aa)||!(b instanceof VarInsnNode bb)||!(c instanceof VarInsnNode cc)||!(d instanceof VarInsnNode dd)
                    ||aa.getOpcode()!=Opcodes.ALOAD||bb.getOpcode()!=Opcodes.ALOAD||cc.getOpcode()!=Opcodes.ASTORE||dd.getOpcode()!=Opcodes.ASTORE
                    ||aa.var!=dd.var||bb.var!=cc.var||aa.var<old.maxLocals||bb.var<old.maxLocals)throw new AssertionError("Unexpected argument adapter");
                for(var n:List.of(a,b,c,d))method.instructions.remove(n);
                method.instructions.set(call,new MethodInsnNode(Opcodes.INVOKEVIRTUAL,"net/minecraft/world/level/Level","getBlockState",
                    "(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/state/BlockState;",false));
            }
        }
        if(redirects!=2)throw new AssertionError("Wrong redirect count "+redirects);
        for(var method:original.methods){var normalized=patched.methods.stream().filter(m->m.name.equals(method.name)&&m.desc.equals(method.desc)).findFirst().orElseThrow();
            normalized.maxLocals=method.maxLocals;normalized.maxStack=method.maxStack;
            if(!Arrays.equals(canonical(method),canonical(normalized)))throw new AssertionError("Other producer code changed: "+method.name);
        }
        System.out.println("PASS actual Yamato Sponge transformation: two ground reads; "+original.methods.size()+" original methods otherwise unchanged; original and patched loops extracted");
    }
    private static MethodNode loop(MethodNode source,String name,String owner,String handler){
        AbstractInsnNode start=null,end=null;
        for(var n:source.instructions){
            if(start==null&&n instanceof TypeInsnNode t&&t.getOpcode()==Opcodes.NEW&&t.desc.equals("net/minecraft/core/BlockPos$MutableBlockPos"))start=n.getPrevious().getPrevious();
            if(start!=null&&n.getOpcode()==Opcodes.I2F&&n.getNext() instanceof VarInsnNode v&&v.getOpcode()==Opcodes.FSTORE&&v.var==6){end=n.getNext();break;}
        }
        if(start==null||end==null||!(start instanceof VarInsnNode v)||v.var!=3)throw new AssertionError("Producer loop shape changed");
        var result=new MethodNode(Opcodes.ACC_PUBLIC|Opcodes.ACC_STATIC,name,"(Lnet/minecraft/world/entity/LivingEntity;FFF)F",null,null);
        for(int[] pair:new int[][]{{3,7},{1,5},{2,6}}){result.visitVarInsn(Opcodes.FLOAD,pair[0]);result.visitVarInsn(Opcodes.FSTORE,pair[1]);}
        result.visitVarInsn(Opcodes.ALOAD,0);result.visitVarInsn(Opcodes.ASTORE,3);
        var labels=new HashMap<LabelNode,LabelNode>();for(var n:source.instructions)if(n instanceof LabelNode l)labels.put(l,new LabelNode());
        for(var n=start;;n=n.getNext()){
            var copy=n.clone(labels);
            if(copy instanceof MethodInsnNode m&&m.owner.equals(owner)&&m.name.equals(handler))m.owner="probe/GroundLoops";
            result.instructions.add(copy);if(n==end)break;
        }
        result.visitVarInsn(Opcodes.FLOAD,6);result.visitInsn(Opcodes.FRETURN);return result;
    }
    private static byte[] canonical(MethodNode method){var w=new ClassWriter(0);w.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"Identity",null,"java/lang/Object",null);method.accept(w);w.visitEnd();return w.toByteArray();}
}
