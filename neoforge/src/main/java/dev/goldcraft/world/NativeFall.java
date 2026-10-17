package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.attribute.EntityAttributes;

/** Publish actual 1.21.1 attributes, including Jump Boost and equipment modifiers. */
public final class NativeFall {
    private static long revision;
    private NativeFall(){}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner){
        if(!GoldCraft.fallAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        double safe=owner.getAttributeValue(EntityAttributes.GENERIC_SAFE_FALL_DISTANCE);
        double multiplier=owner.getAttributeValue(EntityAttributes.GENERIC_FALL_DAMAGE_MULTIPLIER);
        if(!Double.isFinite(safe)||!Double.isFinite(multiplier))return;
        GoldCraft.sendFall(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life())
            .f32((float)Math.clamp(safe,-1024,1024)).f32((float)Math.clamp(multiplier,0,100)).toByteArray());
    }
}
