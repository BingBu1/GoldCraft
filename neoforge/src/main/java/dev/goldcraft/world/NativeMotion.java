package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffects;

/** Motion is published by the same unique effect owner; no second ticker. */
public final class NativeMotion {
    private static long revision;
    private NativeMotion(){}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner){
        if(!GoldCraft.motionAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        int flags=0;float target=0;
        if(owner.hasStatusEffect(StatusEffects.SLOW_FALLING))flags|=1;
        var levitation=owner.getStatusEffect(StatusEffects.LEVITATION);
        if(levitation!=null){flags|=2;target=.05f*(Math.clamp(levitation.getAmplifier(),0,255)+1);}
        if(owner.hasStatusEffect(StatusEffects.DOLPHINS_GRACE))flags|=4;
        float jump=Math.clamp(owner.getJumpBoostVelocityModifier(),0,25.6f);
        GoldCraft.sendMotion(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life()).i32(flags)
            .f32(jump).f32(target).toByteArray());
    }
}
