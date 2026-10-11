import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.ZipFile;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;

/** Uses local producer bytes; redirects only its monotonic clock to a deterministic test source. */
public final class PrepareProducer {
    static byte[] canonical(MethodNode method) {
        var writer=new ClassWriter(0);writer.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"Identity",null,"java/lang/Object",null);
        method.accept(writer);writer.visitEnd();return writer.toByteArray();
    }
    public static void main(String[] args)throws Exception {
        String owner="com/hm/efn/client/screeneffect/BlackWhiteFlashEffect";
        try(var zip=new ZipFile(args[0])) {
            for(String name:List.of(owner,owner+"$ImpactMode",owner+"$BWC_Pipeline",
                                    "com/hm/efn/client/shaderpasses/BlackWhiteContrastSimple")) {
                byte[] bytes=zip.getInputStream(zip.getEntry(name+".class")).readAllBytes();
                var node=new ClassNode();new ClassReader(bytes).accept(node,0);
                if(name.endsWith("$BWC_Pipeline")) {
                    var original=new LinkedHashMap<String,byte[]>();
                    for(var method:node.methods)original.put(method.name+method.desc,canonical(method));
                    int changed=0;
                    for(var method:node.methods)for(var instruction:method.instructions)
                        if(instruction instanceof MethodInsnNode call&&call.owner.equals("java/lang/System")&&call.name.equals("nanoTime")) {
                            if(!method.name.equals("PostEffectHandler")||!call.desc.equals("()J"))throw new AssertionError("Clock site drift");
                            call.owner="probe/Clock";changed++;
                        }
                    if(changed!=1)throw new AssertionError("Clock site count "+changed);
                    var writer=new ClassWriter(0);node.accept(writer);bytes=writer.toByteArray();
                    var verify=new ClassNode();new ClassReader(bytes).accept(verify,0);
                    for(var method:verify.methods) {
                        // Reverse just the declared adapter for a canonical exact-body comparison.
                        for(var instruction:method.instructions)if(instruction instanceof MethodInsnNode call&&call.owner.equals("probe/Clock"))call.owner="java/lang/System";
                        if(!Arrays.equals(original.get(method.name+method.desc),canonical(method)))throw new AssertionError("Producer bytecode changed "+method.name);
                        System.out.println("METHOD_IDENTICAL "+name+"."+method.name+method.desc+" sha256="+
                            HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(canonical(method))));
                    }
                }
                Path output=Path.of(args[1],name+".class");Files.createDirectories(output.getParent());Files.write(output,bytes);
                System.out.println("LOCAL_PRODUCER "+name+" bytes="+bytes.length);
            }
        }
    }
}
