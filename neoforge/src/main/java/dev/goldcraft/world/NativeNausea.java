package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.entity.effect.StatusEffects;
import java.util.Arrays;

/** Vanilla nausea presentation for native viewers without a paired client. */
public final class NativeNausea {
    private static final class State {
        long epoch,generation,tick=Long.MIN_VALUE;
        int serial,spawn,life;
        LivingEntity owner;
        StatusEffectInstance effect;
        float previous,current;
    }
    private static final State[] states=new State[65];
    private static long revision;
    private NativeNausea(){}
    public static void clear(){Arrays.fill(states,null);revision=0;}
    public static void handoff(LivingEntity from,LivingEntity to){
        // Transfer invokes removal hooks to move attributes. It is not a cure.
        if(to!=null)for(var state:states)if(state!=null&&state.owner==from)state.owner=to;
    }
    public static void removed(LivingEntity owner,StatusEffectInstance effect){
        if(owner.getWorld().isClient||!effect.getEffectType().equals(StatusEffects.NAUSEA))return;
        for(var state:states)if(state!=null&&state.owner==owner&&state.effect==effect){
            state.previous=state.current=0;state.effect=null;
        }
    }
    public static void update(HostWorldState.Actor actor,LivingEntity owner,long tick){
        if(!GoldCraft.nauseaAvailable()||!owner.isAlive()||GoldCraft.HOST_WORLD.epoch()==0
            ||actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)return;
        long epoch=GoldCraft.HOST_WORLD.epoch(),generation=GoldCraft.authorityGeneration();
        var s=states[actor.slot()];
        if(s==null||s.epoch!=epoch||s.generation!=generation||s.serial!=actor.serial()||s.spawn!=actor.spawn()
            ||s.life!=actor.life()||tick<s.tick||(s.tick!=Long.MIN_VALUE&&tick-s.tick>1)){
            s=new State();s.epoch=epoch;s.generation=generation;s.serial=actor.serial();s.spawn=actor.spawn();s.life=actor.life();
            states[actor.slot()]=s;
        }
        s.owner=owner;s.effect=owner.getStatusEffect(StatusEffects.NAUSEA);
        if(s.effect==null)s.previous=s.current=0;
        if(s.tick!=tick){
            s.previous=s.current;
            // ClientPlayerEntity.tickNausea; isDurationBelow includes equality (<=60 fades).
            float delta=s.effect!=null&&!s.effect.isDurationBelow(60)?.006666667f:-.05f;
            s.current=Math.clamp(s.current+delta,0,1);s.tick=tick;
        }
        float step=(s.effect!=null?7:20)*(float)Math.PI/180;
        float angle=(float)Math.IEEEremainder((tick%360)*step,Math.PI*2);
        GoldCraft.sendNausea(new Wire.Writer().i64(epoch).i64(++revision)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life())
            .f32(s.previous).f32(s.current).f32(angle).f32(step).toByteArray());
    }
}
