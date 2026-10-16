package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffects;

/** Potion visibility is distinct from the hitbox's permanent renderer hiding. */
public final class NativeVisibility {
    private static long revision;
    private NativeVisibility(){}
    public static boolean invisible(LivingEntity owner){return owner.hasStatusEffect(StatusEffects.INVISIBILITY);}
    public static void clear(){revision=0;}
    public static void update(HostWorldState.Actor actor,LivingEntity owner){
        boolean invisible=invisible(owner);
        if(owner instanceof NativePlayerHitboxEntity box)box.potionInvisible(invisible);
        if(!GoldCraft.visibilityAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        GoldCraft.sendVisibility(new Wire.Writer().i64(GoldCraft.HOST_WORLD.epoch()).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life()).i32(invisible?1:0).toByteArray());
    }
}
