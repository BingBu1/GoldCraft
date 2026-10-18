package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.server.network.ServerPlayerEntity;

/** Publish actual server glow semantics, including command/Mod glow and team color. */
public final class NativeGlow {
    private static long revision;
    private NativeGlow(){}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner,ServerPlayerEntity paired){
        if(!GoldCraft.glowAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        // Effect ownership can move to a native proxy, while scoreboard identity
        // and command-driven glow stay on the authenticated paired player.
        if(paired!=null&&!actor.minecraftPlayer().equals(paired.getUuid()))paired=null;
        boolean enabled=owner.isGlowing()||(paired!=null&&paired.isGlowing());
        int color=(paired!=null?paired:owner).getTeamColorValue();
        GoldCraft.sendGlow(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life())
            .i32(enabled?1:0).i32(enabled?color&0xffffff:0).toByteArray());
    }
}
