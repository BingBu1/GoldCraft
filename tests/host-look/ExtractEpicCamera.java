import java.nio.file.*;
import java.util.*;
import java.util.jar.*;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;

public class ExtractEpicCamera {
    static ClassNode read(JarFile jar,String name)throws Exception{
        var node=new ClassNode();new ClassReader(jar.getInputStream(jar.getJarEntry(name+".class"))).accept(node,0);return node;
    }
    static void copy(JarFile jar,Path output,String owner,Set<String> names)throws Exception{
        var original=read(jar,owner);var stub=new ClassNode();var path=output.resolve(owner+".class");
        new ClassReader(Files.readAllBytes(path)).accept(stub,0);int count=0;
        for(var method:original.methods)if(names.contains(method.name)){
            // Epic's shipped access transformer exposes this vanilla helper.
            if(owner.equals("net/minecraft/world/entity/Entity")&&method.name.equals("getInputVector"))
                method.access=(method.access&~Opcodes.ACC_PRIVATE)|Opcodes.ACC_PUBLIC;
            stub.methods.removeIf(m->m.name.equals(method.name)&&m.desc.equals(method.desc));stub.methods.add(method);count++;
        }
        if(count!=names.size())throw new AssertionError(owner+" method count "+count);
        var writer=new ClassWriter(0);stub.accept(writer);Files.write(path,writer.toByteArray());
    }
    static void method(JarFile jar,String owner,String name,String descriptor)throws Exception{
        if(read(jar,owner).methods.stream().noneMatch(m->m.name.equals(name)&&m.desc.equals(descriptor)&&(m.access&Opcodes.ACC_PUBLIC)!=0))
            throw new AssertionError("Missing public API "+owner+"."+name+descriptor);
    }
    public static void main(String[] args)throws Exception{
        try(var epic=new JarFile(args[0]);var mc=new JarFile(args[1])){
            Path output=Path.of(args[2]);
            copy(epic,output,"yesman/epicfight/api/client/camera/EpicFightCameraAPI",Set.of("turnCamera","lambda$turnCamera$6","getRelativeMove"));
            copy(mc,output,"net/minecraft/world/entity/Entity",Set.of("getInputVector"));
            String caps="yesman/epicfight/world/capabilities/EpicFightCapabilities",patch="yesman/epicfight/world/capabilities/entitypatch/",local="yesman/epicfight/client/world/capabilites/entitypatch/player/LocalPlayerPatch";
            method(epic,caps,"getPlayerPatch","(Lnet/minecraft/world/entity/player/Player;)L"+patch+"player/PlayerPatch;");
            method(epic,caps,"getEntityPatch","(Lnet/minecraft/world/entity/Entity;Ljava/lang/Class;)L"+patch+"EntityPatch;");
            method(epic,local,"getTarget","()Lnet/minecraft/world/entity/LivingEntity;");
            String ui="yesman/epicfight/client/gui/EntityUI",parameters="Lnet/minecraft/world/entity/LivingEntity;L"+patch+"LivingEntityPatch;L"+local+";";
            method(epic,ui,"shouldDraw","("+parameters+"F)Z");
            method(epic,ui,"draw","("+parameters+"Lcom/mojang/blaze3d/vertex/PoseStack;Lnet/minecraft/client/renderer/MultiBufferSource;F)V");
            method(epic,"yesman/epicfight/world/gamerule/EpicFightGameRules$ConfigurableGameRule","getRuleValue","(Lnet/minecraft/world/level/Level;)Ljava/lang/Object;");
            System.out.println("PASS exact pinned Epic indicator API descriptors; extracted 3 real Epic camera methods and Minecraft input-vector method.");
        }
    }
}
