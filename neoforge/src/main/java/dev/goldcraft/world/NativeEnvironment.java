package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffects;

/** Separate negotiated message: old attribute packets retain their exact layout. */
public final class NativeEnvironment {
    private static long revision;
    private NativeEnvironment(){}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner){
        if(!GoldCraft.environmentAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        int flags=0;
        if(owner.hasStatusEffect(StatusEffects.FIRE_RESISTANCE))flags|=1;
        if(owner.hasStatusEffect(StatusEffects.WATER_BREATHING)||owner.hasStatusEffect(StatusEffects.CONDUIT_POWER))flags|=2;
        if(owner.hasStatusEffect(StatusEffects.SLOW_FALLING)||owner.hasStatusEffect(StatusEffects.LEVITATION))flags|=4;
        GoldCraft.sendEnvironment(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life()).i32(flags).toByteArray());
    }
}
