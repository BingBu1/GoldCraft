package dev.goldcraft.world;

import net.minecraft.network.packet.CustomPayload;
import net.minecraft.entity.Entity;
import net.minecraft.server.network.ServerPlayerEntity;
import net.neoforged.neoforge.network.PacketDistributor;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.stream.Stream;

/** Restore Epic's local skill binding before callbacks and after player replacement. */
public final class EpicSkillSync {
    private EpicSkillSync() {}

    // Optional Epic types stay out of GoldCraft's class descriptors. Resolve only
    // when its matching mixins run; these are transition paths, never draw/tick loops.
    private static final class Api {
        static final Api INSTANCE=new Api();
        final MethodHandle executor,original,slot,disabled,localPacket,enable,skills,containers,category,shouldSave;
        Api() {
            try {
                Class<?> container=Class.forName("yesman.epicfight.skill.SkillContainer");
                Class<?> patch=Class.forName("yesman.epicfight.world.capabilities.entitypatch.player.PlayerPatch");
                Class<?> slotType=Class.forName("yesman.epicfight.skill.SkillSlot");
                Class<?> skillsType=Class.forName("yesman.epicfight.world.capabilities.skill.PlayerSkills");
                Class<?> categoryType=Class.forName("yesman.epicfight.skill.SkillCategory");
                Class<?> bindingType=Class.forName("yesman.epicfight.network.server.SPChangeSkill");
                Class<?> valueType=Class.forName("yesman.epicfight.network.server.SPSetSkillContainerValue");
                var lookup=MethodHandles.publicLookup();
                // Exact descriptors avoid Class.getMethod enumerating unrelated
                // client-only methods on Epic classes in a dedicated server.
                executor=lookup.findVirtual(container,"getExecutor",MethodType.methodType(patch));
                original=lookup.findVirtual(patch,"getOriginal",MethodType.methodType(Entity.class));
                slot=lookup.findVirtual(container,"getSlot",MethodType.methodType(slotType));
                disabled=lookup.findVirtual(container,"isDisabled",MethodType.methodType(boolean.class));
                localPacket=lookup.findVirtual(container,"createSyncPacketToLocalPlayer",MethodType.methodType(bindingType));
                enable=lookup.findStatic(valueType,"enable",MethodType.methodType(valueType,slotType,boolean.class,int.class));
                skills=lookup.findVirtual(patch,"getPlayerSkills",MethodType.methodType(skillsType));
                containers=lookup.findVirtual(skillsType,"listSkillContainers",MethodType.methodType(Stream.class));
                category=lookup.findVirtual(slotType,"category",MethodType.methodType(categoryType));
                shouldSave=lookup.findVirtual(categoryType,"shouldSave",MethodType.methodType(boolean.class));
            } catch(ReflectiveOperationException e) {throw new IllegalStateException("Unsupported Epic skill synchronization ABI",e);}
        }
        void send(Object container,ServerPlayerEntity player,boolean isDisabled) throws Throwable {
            CustomPayload binding=(CustomPayload)localPacket.invoke(container);
            CustomPayload state=(CustomPayload)enable.invoke(slot.invoke(container),isDisabled,player.getId());
            // SPChangeSkill enables the slot unconditionally. Restore the actual
            // disabled flag in the same ordered bundle, including retained skills.
            PacketDistributor.sendToPlayer(player,binding,state);
        }
    }

    public static void beforeInitiate(Object container,boolean initialize) {
        Api api=Api.INSTANCE;
        try {
            Object owner=api.original.invoke(api.executor.invoke(container));
            if(!(owner instanceof ServerPlayerEntity player)||player.networkHandler==null
                    ||player.networkHandler.player!=player||player.isRemoved())return;
            // A clone is not yet the connection's player and has a temporary ID.
            // Its binding is sent by joined(), after the vanilla respawn packet.
            api.send(container,player,!initialize&&(boolean)api.disabled.invoke(container));
        } catch(Throwable e) {throw new IllegalStateException("Epic skill pre-initialization sync failed",e);}
    }

    public static void joined(Object patch) {
        Api api=Api.INSTANCE;
        try {
            Object owner=api.original.invoke(patch);
            if(!(owner instanceof ServerPlayerEntity player)||player.networkHandler==null)return;
            try(Stream<?> all=(Stream<?>)api.containers.invoke(api.skills.invoke(patch))) {
                var iterator=all.iterator();
                while(iterator.hasNext()) {
                    Object container=iterator.next();
                    if(!(boolean)api.shouldSave.invoke(api.category.invoke(api.slot.invoke(container))))
                        api.send(container,player,(boolean)api.disabled.invoke(container));
                }
            }
        } catch(Throwable e) {throw new IllegalStateException("Epic skill world-join sync failed",e);}
    }
}
