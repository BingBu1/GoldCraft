package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.util.math.MathHelper;

/** Read the unique authoritative effect ticker, including vanilla darkness fading. */
public final class NativeVision {
    private static long revision;
    private NativeVision(){}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner){
        if(!GoldCraft.visionAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        float night=0,blindness=0,darkness=0,pulse=0;
        var vision=owner.getStatusEffect(StatusEffects.NIGHT_VISION);
        if(vision!=null)night=vision.isDurationBelow(200)
            ?.7f+MathHelper.sin(vision.getDuration()*(float)Math.PI*.2f)*.3f:1;
        var blind=owner.getStatusEffect(StatusEffects.BLINDNESS);
        if(blind!=null)blindness=blind.isInfinite()?1:Math.clamp(blind.getDuration()/20f,0,1);
        var dark=owner.getStatusEffect(StatusEffects.DARKNESS);
        if(dark!=null){
            darkness=Math.clamp(dark.getFadeFactor(owner,1),0,1);
            pulse=Math.max(0,MathHelper.cos(owner.age*(float)Math.PI*.025f)*.45f*darkness);
        }
        GoldCraft.sendVision(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life())
            .f32(night).f32(blindness).f32(darkness).f32(pulse).toByteArray());
    }
}
