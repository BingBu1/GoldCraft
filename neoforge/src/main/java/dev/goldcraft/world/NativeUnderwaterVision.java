package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.registry.tag.FluidTags;
import java.util.Arrays;

/** Vanilla ClientPlayerEntity water adaptation, sourced from server authority. */
public final class NativeUnderwaterVision {
    private static final class State {
        long epoch,generation,tick=Long.MIN_VALUE;
        int serial,spawn,life,underwaterTicks;
    }
    private static final State[] states=new State[65];
    private NativeUnderwaterVision(){}
    public static void clear(){Arrays.fill(states,null);}
    public static float strength(HostWorldState.Actor actor,LivingEntity owner){
        long tick=owner.getServer().getTicks(),epoch=GoldCraft.HOST_WORLD.epoch(),generation=GoldCraft.authorityGeneration();
        boolean water;
        if(actor.minecraftForm())water=owner.isSubmergedIn(FluidTags.WATER);
        else {
            var view=GoldCraft.nativeViewAvailable()?GoldCraft.HOST_WORLD.nativeView().get(GoldCraft.HOST_WORLD,actor,tick):null;
            if(view==null){states[actor.slot()]=null;return 0;}
            water=view.water();
        }
        var s=states[actor.slot()];
        if(s==null||s.epoch!=epoch||s.generation!=generation||s.serial!=actor.serial()||s.spawn!=actor.spawn()
            ||s.life!=actor.life()||tick<s.tick||(s.tick!=Long.MIN_VALUE&&tick-s.tick>1)){
            s=new State();s.epoch=epoch;s.generation=generation;s.serial=actor.serial();s.spawn=actor.spawn();s.life=actor.life();
            states[actor.slot()]=s;
        }
        if(s.tick!=tick){
            s.underwaterTicks=Math.clamp(s.underwaterTicks+(water?(owner.isSpectator()?10:1):-10),0,600);s.tick=tick;
        }
        // Adaptation runs even before the effect is acquired, as on vanilla's
        // local player. On leaving water brightness stops immediately.
        if(!water||!owner.hasStatusEffect(StatusEffects.CONDUIT_POWER))return 0;
        if(s.underwaterTicks>=600)return 1;
        float early=Math.clamp(s.underwaterTicks/100f,0,1);
        float late=s.underwaterTicks<100?0:Math.clamp((s.underwaterTicks-100)/500f,0,1);
        return early*.6f+late*.39999998f;
    }
}
