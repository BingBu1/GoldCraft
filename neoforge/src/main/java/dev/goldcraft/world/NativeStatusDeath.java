package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.entity.Entity;
import net.minecraft.server.network.ServerPlayerEntity;

/** Run removal effects once, only after the native server confirms this spawn died. */
public final class NativeStatusDeath {
    private NativeStatusDeath(){}
    private static boolean claim(NativePlayerHitboxEntity box,HostWorldState host,HostWorldState.Actor dead){
        if(box==null||box.isRemoved()||box.body()==null||box.body().isRemoved()||dead==null)return false;
        var body=box.body();var previous=body.actor();
        if(host.epoch()==0||body.epoch()!=host.epoch()||previous.slot()!=dead.slot()
            ||previous.serial()!=dead.serial()||previous.spawn()!=dead.spawn()||dead.spawn()==0
            ||previous.life()==0||(previous.flags()&5)!=1||(dead.flags()&1)!=0
            ||(dead.team()!=1&&dead.team()!=2))return false;
        return box.claimNativeDeath();
    }
    public static void prepare(ServerPlayerEntity player,HostWorldState host,HostWorldState.Actor dead){
        var box=NativePlayers.hitbox(dead);
        if(box==null||!player.getUuid().equals(dead.minecraftPlayer())||player.getWorld()!=box.getWorld())return;
        if(claim(box,host,dead)){
            // The real player subsequently runs onDeath and KILLED removal.
            // Move the same instances, including hidden effects, to that corpse.
            NativeStatusEffects.transfer(box,player);
        }
    }
    public static void retire(NativePlayerHitboxEntity box,HostWorldState host,HostWorldState.Actor dead){
        if(!claim(box,host,dead))return;
        var body=box.body();
        // A dead snapshot can already contain the spectator target's position.
        // Keep the last living body and vanilla player-specific effect rules.
        NativeStatusEffects.transfer(box,body);
        body.setHealth(0);
        body.remove(Entity.RemovalReason.KILLED);
        // Native TakeDamage owns kill/rewards; do not replay a loot/death pipeline.
    }
}
