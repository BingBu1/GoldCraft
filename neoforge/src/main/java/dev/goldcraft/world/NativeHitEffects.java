package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.effect.StatusEffectInstance;
import java.util.ArrayList;
import java.util.List;

/** Hurt callbacks belong to a confirmed hit, not a speculative native health request. */
public final class NativeHitEffects implements AutoCloseable {
    private static final ThreadLocal<NativeHitEffects> CURRENT=new ThreadLocal<>();
    private record Callback(StatusEffectInstance effect,DamageSource source,float amount){}
    private final NativeHitEffects previous;
    private final NativePlayerHitboxEntity target;
    private final long epoch,generation;
    private final int serial,spawn;
    private final List<Callback> callbacks=new ArrayList<>();
    private int requests,receipts;
    private boolean closed,finished,ready;
    private NativeHitEffects(NativePlayerHitboxEntity target){
        this.target=target;previous=CURRENT.get();
        var actor=target.body().actor();epoch=target.body().epoch();generation=GoldCraft.authorityGeneration();
        serial=actor.serial();spawn=actor.spawn();CURRENT.set(this);
    }
    public static NativeHitEffects begin(NativePlayerHitboxEntity target){return new NativeHitEffects(target);}
    static NativeHitEffects request(NativePlayerEntity body){
        var hit=CURRENT.get();
        if(hit==null||hit.target.body()!=body)return null;
        hit.requests++;return hit;
    }
    public static boolean capture(LivingEntity target,StatusEffectInstance effect,DamageSource source,float amount){
        var hit=CURRENT.get();
        if(hit==null||hit.target!=target)return false;
        // Freeze amplifier/origin at impact; upgrades while waiting for the host
        // must not retroactively strengthen an earlier hit.
        hit.callbacks.add(new Callback(new StatusEffectInstance(effect),source,amount));return true;
    }
    public void localResult(boolean accepted,boolean absorptionPaid){
        ready=accepted;close();
        if(requests==0)complete(accepted&&absorptionPaid);
    }
    private LivingEntity recipient(){
        var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==target.hostSlot()).findFirst().orElse(null);
        if(epoch!=GoldCraft.HOST_WORLD.epoch()||generation!=GoldCraft.authorityGeneration()
            ||actor==null||actor.serial()!=serial||actor.spawn()!=spawn)return null;
        // A form switch preserves the native spawn. Resolve its current effect
        // owner without creating another body or applying to a respawn.
        if(actor.minecraftForm()&&(actor.flags()&32)!=0){
            var player=target.getServer().getPlayerManager().getPlayer(actor.minecraftPlayer());
            return player!=null&&!player.isRemoved()&&player.getWorld()==target.getWorld()?player:null;
        }
        var box=NativePlayers.hitbox(actor);
        return box!=null&&!box.isRemoved()&&box.getWorld()==target.getWorld()?box:null;
    }
    void complete(boolean accepted){
        if(finished)return;
        // A Mod can emit more than one native delta within one hurt call.
        // Any accepted delta confirms the hit; one rejected delta alone does
        // not cancel a later accepted delta, and callbacks still run only once.
        if(requests>0&&++receipts<requests&&!accepted)return;
        finished=true;
        var calls=List.copyOf(callbacks);callbacks.clear();
        if(!accepted||!closed||!ready)return;
        // Replay outside any surrounding hurt scope. A nested damage call opens
        // its own scope normally and waits for its own authority receipt.
        var surrounding=CURRENT.get();CURRENT.remove();
        try{
            for(var call:calls){
                var owner=recipient();if(owner==null)return;
                try(var scope=NativeStatusExecution.effect(owner,call.effect(),false)){
                    call.effect().onEntityDamage(owner,call.source(),call.amount());
                }
            }
        }finally{if(surrounding==null)CURRENT.remove();else CURRENT.set(surrounding);}
    }
    @Override public void close(){
        if(closed)return;
        closed=true;
        if(!ready)callbacks.clear();
        if(previous==null)CURRENT.remove();else CURRENT.set(previous);
    }
}
