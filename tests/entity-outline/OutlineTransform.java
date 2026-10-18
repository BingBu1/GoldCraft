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

/** Apply the shipped accessors to the exact game classes, including the private superclass. */
public final class OutlineTransform {
    public static void main(String[] args)throws Exception {
        MixinBootstrap.init();var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.CLIENT);
        Mixins.addConfiguration("outline.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();ctor.setAccessible(true);
        var transformer=(IMixinTransformer)ctor.newInstance();boolean production=args[2].equals("production");
        String[] owners=production?new String[]{"net/minecraft/client/renderer/RenderStateShard$BooleanStateShard","net/minecraft/client/renderer/RenderType$CompositeState"}:
            new String[]{"net/minecraft/client/render/RenderPhase$Toggleable","net/minecraft/client/render/RenderLayer$MultiPhaseParameters"};
        int total=0,accessors=0;
        try(var jar=new ZipFile(args[0])){
            for(String owner:owners){
                var entry=jar.getEntry(owner+".class");
                // NeoForge's patched client is an overlay; unchanged nested
                // classes still come from the pinned base game on its classpath.
                byte[] before;
                try(var in=entry!=null?jar.getInputStream(entry):OutlineTransform.class.getClassLoader().getResourceAsStream(owner+".class")){
                    if(in==null)throw new AssertionError("Missing pinned class: "+owner);before=in.readAllBytes();
                }
                System.out.println("target "+owner+" sha256="+HexFormat.of().formatHex(java.security.MessageDigest.getInstance("SHA-256").digest(before)));
                byte[] after=transformer.transformClass(env,owner.replace('/','.'),before);if(Arrays.equals(before,after))throw new AssertionError("not transformed: "+owner);
                var node=new ClassNode();new ClassReader(after).accept(node,0);
                for(var method:node.methods){
                    if((method.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0){new Analyzer<>(new BasicVerifier()).analyze(owner,method);total++;}
                    if(method.name.startsWith("goldcraft$")){
                        accessors++;boolean field=false;
                        for(var insn:method.instructions)if(insn instanceof FieldInsnNode f&&f.getOpcode()==Opcodes.GETFIELD&&f.owner.equals(owner)){
                            field=node.fields.stream().anyMatch(x->x.name.equals(f.name)&&x.desc.equals(f.desc));
                            if(method.name.equals("goldcraft$enabled")&&!f.desc.equals("Z"))throw new AssertionError("Not boolean");
                        }
                        if(!field)throw new AssertionError("Accessor did not read a real declared field: "+method.name);
                    }
                }
                Path path=Path.of(args[1],owner+".class");Files.createDirectories(path.getParent());Files.write(path,after);
            }
        }
        if(accessors!=5)throw new AssertionError("accessors="+accessors);
        System.out.println("PASS "+args[2]+" actual Sponge outline accessors="+accessors+" methods="+total);
    }
}
