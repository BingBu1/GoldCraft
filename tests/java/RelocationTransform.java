package probe;

import java.nio.file.*;
import java.util.*;
import java.util.zip.ZipFile;
import org.objectweb.asm.*;
import org.objectweb.asm.tree.*;
import org.objectweb.asm.tree.analysis.*;
import org.spongepowered.asm.launch.MixinBootstrap;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.transformer.IMixinTransformer;
import com.llamalad7.mixinextras.MixinExtrasBootstrap;

/** Real Sponge injection, followed by vanilla request/ACK bytecode execution
 * against explicit world/player/network observers. No game classes are copied
 * into published sources or loaded by this transformer. */
public final class RelocationTransform {
    private static final String OWNER="net/minecraft/server/network/ServerPlayNetworkHandler";
    private static byte[] canonical(MethodNode method) {
        var copy=new MethodNode(method.access,method.name,method.desc,method.signature,
            method.exceptions.toArray(String[]::new));method.accept(copy);
        for(var instruction:copy.instructions.toArray())if(instruction instanceof FrameNode)copy.instructions.remove(instruction);
        copy.maxStack=0;copy.maxLocals=0;
        var writer=new ClassWriter(0);
        writer.visit(Opcodes.V21,Opcodes.ACC_PUBLIC,"Identity",null,"java/lang/Object",null);
        copy.accept(writer);writer.visitEnd();return writer.toByteArray();
    }
    private static MethodNode method(ClassNode node,String name,String desc) {
        return node.methods.stream().filter(m->m.name.equals(name)&&m.desc.equals(desc)).findFirst().orElseThrow();
    }
    private static ClassNode read(byte[] bytes) {
        var node=new ClassNode();new ClassReader(bytes).accept(node,ClassReader.SKIP_DEBUG);return node;
    }
    public static void main(String[] args)throws Exception {
        MixinBootstrap.init();
        var env=MixinEnvironment.getCurrentEnvironment();env.setSide(MixinEnvironment.Side.SERVER);
        Mixins.addConfiguration("relocation.mixins.json");
        var ctor=Class.forName("org.spongepowered.asm.mixin.transformer.MixinTransformer").getDeclaredConstructor();
        ctor.setAccessible(true);var transformer=(IMixinTransformer)ctor.newInstance();
        MixinEnvironment.getDefaultEnvironment().setActiveTransformer(transformer);
        MixinExtrasBootstrap.init();
        byte[] before;
        try(var jar=new ZipFile(args[0])) {before=jar.getInputStream(jar.getEntry(OWNER+".class")).readAllBytes();}
        byte[] after=transformer.transformClass(env,OWNER.replace('/','.'),before);
        var original=read(before);var patched=read(after);
        int untouched=0,verified=0;
        var changed=new HashSet<String>();
        for(var old:original.methods) {
            var next=method(patched,old.name,old.desc);
            if(Arrays.equals(canonical(old),canonical(next)))untouched++;
            else changed.add(old.name+old.desc);
        }
        var expected=Set.of("onPlayerMove(Lnet/minecraft/network/packet/c2s/play/PlayerMoveC2SPacket;)V",
            "handlePendingTeleport()Z","requestTeleport(DDDFFLjava/util/Set;)V");
        if(!changed.equals(expected))throw new AssertionError("Unexpected injection changes: "+changed);
        for(var m:patched.methods)if((m.access&(Opcodes.ACC_ABSTRACT|Opcodes.ACC_NATIVE))==0) {
            new Analyzer<>(new BasicVerifier()).analyze(patched.name,m);verified++;
        }
        String request="(DDDFFLjava/util/Set;)V";
        var injected=patched.methods.stream().filter(m->m.name.contains("goldcraft$requestTeleport")).findFirst().orElseThrow();
        int hooks=0,coordinates=0;
        for(var insn:method(patched,"requestTeleport",request).instructions)
            if(insn instanceof MethodInsnNode c&&c.name.equals(injected.name))hooks++;
        for(var insn:injected.instructions)if(insn instanceof MethodInsnNode c&&
            c.owner.equals("dev/goldcraft/world/HostPlayerEffects")&&c.name.equals("teleporting")&&
            c.desc.equals("(Lnet/minecraft/server/network/ServerPlayerEntity;DDD)V"))coordinates++;
        if(hooks!=1||coordinates!=1)throw new AssertionError("Absolute destination hook missing");
        var fixturePath=Path.of(args[1],OWNER+".class");var fixture=read(Files.readAllBytes(fixturePath));
        // Transplant intact vanilla methods and the actual injected HEAD handler.
        // The fixture only supplies the API boundaries and observed fields.
        var copies=List.of(method(patched,"requestTeleport","(DDDFF)V"),
            method(patched,"requestTeleport",request),
            method(patched,"onTeleportConfirm","(Lnet/minecraft/network/packet/c2s/play/TeleportConfirmC2SPacket;)V"),injected);
        for(var copy:copies) {
            fixture.methods.removeIf(m->m.name.equals(copy.name)&&m.desc.equals(copy.desc));fixture.methods.add(copy);
        }
        var writer=new ClassWriter(ClassWriter.COMPUTE_MAXS);fixture.accept(writer);
        Files.write(fixturePath,writer.toByteArray());
        String player="net/minecraft/server/network/ServerPlayerEntity";
        ClassNode vanillaPlayer;
        try(var jar=new ZipFile(args[0])){vanillaPlayer=read(jar.getInputStream(jar.getEntry(player+".class")).readAllBytes());}
        var riding=method(vanillaPlayer,"startRiding","(Lnet/minecraft/entity/Entity;Z)Z");
        new Analyzer<>(new BasicVerifier()).analyze(player,riding);
        var playerPath=Path.of(args[1],player+".class");var observedPlayer=read(Files.readAllBytes(playerPath));
        observedPlayer.methods.removeIf(m->m.name.equals(riding.name)&&m.desc.equals(riding.desc));observedPlayer.methods.add(riding);
        var playerWriter=new ClassWriter(ClassWriter.COMPUTE_MAXS);observedPlayer.accept(playerWriter);Files.write(playerPath,playerWriter.toByteArray());
        // Keep transformed third-party bytecode only in the caller's build evidence.
        Files.write(Path.of(args[2],"transformed-handler.class"),after);
        System.out.println("PASS real Sponge: exact 3 changed vanilla methods, "+untouched+
            " untouched including ACK/vehicle handler, "+verified+" verified; transplanted real request/ACK/startRiding and destination hook");
    }
}
