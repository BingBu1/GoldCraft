package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.VitalsLedger;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.registry.tag.DamageTypeTags;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

/** Native health is authoritative in both forms; only the MC server produces deltas. */
public final class SharedVitals {
    private static final float SCALE=MinecraftObjects.CS_HEALTH_PER_MC;
    private static final Map<UUID,State> states=new HashMap<>();
    private static long sequence;
    private static final class State {
        long epoch,sent;
        int slot,serial,spawn;
        ServerPlayerEntity entity;
        float observed;
        boolean active,confirming,deathConfirmed;
        DamageSource deathSource;
        final VitalsLedger ledger=new VitalsLedger();
    }
    private SharedVitals(){}
    public static void clear(){states.clear();sequence=0;}
    public static void disconnect(UUID uuid){states.remove(uuid);}
    public static boolean deferDeath(ServerPlayerEntity player,DamageSource source){
        var state=states.get(player.getUuid());
        if(state==null||state.entity!=player||!state.active||state.confirming)return false;
        state.deathSource=source;return true;
    }
    public static boolean nativeRespawnOwned(ServerPlayerEntity player){
        return GoldCraft.hostConnected()&&GoldCraft.HOST_WORLD.actor(player.getUuid())!=null;
    }
    public static boolean deferRemoval(ServerPlayerEntity player){
        var state=states.get(player.getUuid());
        return state!=null&&state.entity==player&&!state.deathConfirmed&&state.deathSource!=null;
    }
    /** Runs before draining the next host snapshots, using the prior form/spawn. */
    public static void capture(MinecraftServer server,HostWorldState host){
        for(var entry:states.entrySet()){
            State state=entry.getValue();var player=server.getPlayerManager().getPlayer(entry.getKey());
            if(player!=state.entity||state.epoch!=host.epoch())continue;
            if(state.active&&!state.deathConfirmed){
                float health=player.getHealth(),delta=(health-state.observed)*SCALE;
                if(Float.isFinite(delta)&&Math.abs(delta)>0.00001f){
                    var source=player.getRecentDamageSource();
                    var attacker=delta<0&&source!=null?source.getAttacker():null;
                    HostWorldState.Actor actor=attacker instanceof NativePlayerEntity p?p.actor()
                        :attacker instanceof ServerPlayerEntity p?host.actor(p.getUuid()):null;
                    int kind=delta<0&&source!=null?(source.isIn(DamageTypeTags.IS_EXPLOSION)?3:source.isIn(DamageTypeTags.IS_PROJECTILE)?2:attacker!=null?1:0):0;
                    long event=++sequence;
                    byte[] payload=new Wire.Writer().i64(state.epoch).i64(event).i32(state.slot).i32(state.serial).i32(state.spawn)
                        .bytes(Wire.uuid(entry.getKey())).f32(delta)
                        .i32(actor==null?0:actor.slot()).i32(actor==null?0:actor.serial()).i32(actor==null?0:actor.spawn()).i32(kind)
                        .i64(MinecraftObjects.key(attacker)).toByteArray();
                    state.ledger.add(new VitalsLedger.Change(event,delta,payload));
                }
                state.observed=health;
            }
            for(var change:state.ledger.pending()){
                if(Long.compareUnsigned(change.sequence(),state.sent)>0){
                    if(!GoldCraft.sendToHost(Wire.VITALS_DELTA,change.payload()))break;
                    state.sent=change.sequence();
                }
            }
        }
    }
    /** Returns true only while a speculative MC death awaits native confirmation. */
    public static boolean prepare(ServerPlayerEntity player,HostWorldState host,HostWorldState.Actor actor){
        State state=states.get(player.getUuid());
        if(state==null||state.epoch!=host.epoch()||state.serial!=actor.serial()||state.spawn!=actor.spawn()){
            boolean died=state!=null&&state.entity==player&&state.deathConfirmed;
            state=new State();state.epoch=host.epoch();state.slot=actor.slot();state.serial=actor.serial();state.spawn=actor.spawn();
            // Reset deltas for the new native spawn, while keeping the old MC
            // body dead until HostSession performs vanilla PlayerManager respawn.
            // This matters when CS respawns before MC's 20-tick corpse removal.
            state.entity=player;state.deathConfirmed=died;states.put(player.getUuid(),state);
        }
        if(state.entity!=player){state.entity=player;state.deathConfirmed=false;state.deathSource=null;}
        state.ledger.snapshot(actor.health(),actor.vitalsAck());
        boolean alive=(actor.flags()&5)==1&&actor.health()>0;
        // ReGameDLL Killed leaves pev->health == 1; the alive/deadflag-derived
        // actor flag, not the numeric health alone, determines native death.
        if(!alive&&actor.spawn()!=0&&(actor.team()==1||actor.team()==2)){
            if(!state.deathConfirmed){
                state.confirming=true;
                try{
                    player.setHealth(0);
                    player.onDeath(state.deathSource!=null?state.deathSource:player.getDamageSources().genericKill());
                    state.deathConfirmed=true;
                }finally{state.confirming=false;}
            }
        }else if(!state.deathConfirmed){
            player.setHealth(state.ledger.health(player.getMaxHealth()*SCALE)/SCALE);
            if(player.isAlive())state.deathSource=null;
        }
        state.observed=player.getHealth();
        return alive&&player.getHealth()<=0&&!state.deathConfirmed&&state.ledger.size()!=0;
    }
    public static void active(ServerPlayerEntity player,boolean enabled){
        var state=states.get(player.getUuid());
        if(state!=null&&state.entity==player){state.active=enabled;state.observed=player.getHealth();}
    }
    public static int pending(UUID uuid){var state=states.get(uuid);return state==null?0:state.ledger.size();}
}
