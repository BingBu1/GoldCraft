package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostEffectGrant;
import dev.goldcraft.bridge.HostEffects;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.LocalQpcClock;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.mixin.ServerPlayNetworkHandlerAccessor;
import dev.goldcraft.net.HostPayload;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.GameMode;
import net.minecraft.entity.Entity;
import java.util.*;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/** Server-owned pairing lifecycle and position publication. Vanilla movement validation stays enabled. */
public final class HostSession {
    private static final Map<UUID,Player> PLAYERS=new HashMap<>();
    private record Held(GameMode mode,boolean flying) {}
    private static final Map<UUID,Held> HELD=new HashMap<>();
    private static final String HOLD_TAG="goldcraft_waiting_";
    // A local Minecraft client can reconnect while its native CS player stays alive.
    // Never restart that player's pose sequence merely because its client disconnected.
    private static long poseSequence,lastDiagnostic;
    private static final int MAX_PLAYERS=64;
    private static final long REOPEN_INTERVAL_NANOS=250_000_000L;
    // Fixed slot history survives client disconnects/re-pairing in this authority
    // connection. Dropping a Player must not let a delayed OPEN revive its stream.
    private static final StreamHistory[] EFFECT_STREAMS=new StreamHistory[MAX_PLAYERS];
    private static long effectGeneration,effectServiceGeneration,effectNativeConnection;
    private static final class StreamHistory {
        long stream;
        HostEffects.Target target;
        UUID uuid;
        HostEffectGrant.Grant grant;
        boolean retired;
    }
    private static final class Player {
        long epoch,lastReady;
        int serial,life;
        ServerPlayerEntity entity;
        boolean ready,placed,alive,enabled;
        HostEffectGrant.Grant grant,reopen;
        HostPlayerEffects.Binding effects;
        ServerPlayerEntity effectPlayer;
        ServerPlayNetworkHandler effectHandler;
        long effectGeneration,nextReopen;
        boolean effectAligned,effectsReady,effectTeleportAllowed;
    }
    private HostSession(){}
    public static void clear(MinecraftServer server){
        HostPlayerEffects.clear();Arrays.fill(EFFECT_STREAMS,null);
        effectGeneration=effectServiceGeneration=effectNativeConnection=0;
        for(var uuid:PLAYERS.keySet()){var player=server.getPlayerManager().getPlayer(uuid);if(player!=null)hold(player);}
        PLAYERS.clear();
    }
    public static void disconnect(ServerPlayerEntity player){
        Player state=PLAYERS.get(player.getUuid());
        if(!GoldCraft.effectsNegotiated()||state==null||state.entity==null||state.entity==player){
            if(state!=null)retireEffects(state,false);
            release(player);PLAYERS.remove(player.getUuid());SharedVitals.disconnect(player.getUuid());
        }
    }
    public static void stopped(){
        HostPlayerEffects.clear();Arrays.fill(EFFECT_STREAMS,null);
        effectGeneration=effectServiceGeneration=effectNativeConnection=0;PLAYERS.clear();HELD.clear();
    }
    public static void hold(ServerPlayerEntity player) {
        HELD.computeIfAbsent(player.getUuid(),ignored-> {
            // Persist the previous mode if a server saves while waiting for the host.
            for(String tag:player.getCommandTags())if(tag.matches(HOLD_TAG+"[0-3]_[01]")) {
                String[] fields=tag.substring(HOLD_TAG.length()).split("_");
                return new Held(GameMode.byId(Integer.parseInt(fields[0])),fields[1].equals("1"));
            }
            var held=new Held(player.interactionManager.getGameMode(),player.getAbilities().flying);
            player.addCommandTag(HOLD_TAG+held.mode.getId()+"_"+(held.flying?1:0));return held;
        });
        player.changeGameMode(GameMode.SPECTATOR);player.setVelocity(0,0,0);
    }
    private static void release(ServerPlayerEntity player) {
        var held=HELD.remove(player.getUuid());if(held==null)return;
        player.changeGameMode(held.mode);player.getAbilities().flying=held.flying&&player.getAbilities().allowFlying;
        player.sendAbilitiesUpdate();
        for(String tag:List.copyOf(player.getCommandTags()))if(tag.matches(HOLD_TAG+"[0-3]_[01]"))player.removeCommandTag(tag);
    }
    static void confirmedDeath(ServerPlayerEntity player,net.minecraft.entity.damage.DamageSource source) {
        var held=HELD.get(player.getUuid());
        if(held==null||!player.isSpectator()||held.mode==GameMode.SPECTATOR){
            player.onDeath(source);return;
        }
        // Native-form players are held as spectators only for presentation.
        // Vanilla death/drop/XP rules must use their saved gameplay mode.
        // Keep the hold record and tag so respawn restores that same mode.
        try{
            player.changeGameMode(held.mode);
            player.onDeath(source);
        }finally{player.changeGameMode(GameMode.SPECTATOR);}
    }
    // Called only for a newly accepted binding, including a reused slot in the same map.
    public static void paired(UUID uuid,long epoch){
        Player previous=PLAYERS.get(uuid);
        if(previous!=null)retireEffects(previous,false);
        else if(PLAYERS.size()>=MAX_PLAYERS)return;
        Player p=new Player();p.epoch=epoch;PLAYERS.put(uuid,p);
    }
    public static void ready(UUID uuid,long epoch,int serial,int life,boolean ready){
        Player p=PLAYERS.get(uuid);
        if(p!=null&&p.epoch==epoch&&p.serial==serial&&p.life==life&&life!=0){p.ready=ready;p.lastReady=System.nanoTime();}
    }
    public static void respawned(ServerPlayerEntity newPlayer){
        Player state=PLAYERS.get(newPlayer.getUuid());if(state==null)return;
        retireEffects(state,false);
        // Vanilla preserves the numeric entity ID across respawn. Track the actual
        // entity instance, and wait until the caller updates networkHandler.player.
        state.entity=newPlayer;state.ready=state.placed=state.enabled=false;hold(newPlayer);
    }
    private static boolean teleportPending(ServerPlayerEntity player){
        return player.networkHandler==null||((ServerPlayNetworkHandlerAccessor)player.networkHandler).goldcraft$requestedTeleportPos()!=null;
    }
    private static void place(ServerPlayerEntity player,ServerWorld world,HostWorldState.Actor actor){
        var feet=actor.minecraftFeet();
        effectCorrection(player,()->player.teleport(world,feet.x(),feet.y(),feet.z(),-actor.yaw()-90,actor.pitch()));
        player.setVelocity(0,0,0);player.fallDistance=0;
        player.networkHandler.syncWithPlayerPosition();
    }
    private static boolean hostAlive(HostWorldState.Actor actor){
        return actor.life()!=0&&(actor.flags()&1)!=0&&(actor.flags()&4)==0&&actor.team()!=0&&actor.team()!=3;
    }
    private static boolean effectTarget(HostEffectGrant.Grant grant,HostWorldState host,HostWorldState.Actor actor,UUID uuid){
        if(grant==null||actor==null)return false;
        var target=grant.snapshot().target();
        return target.world()==host.epoch()&&target.kind()==HostEffects.PLAYER&&target.key()==actor.slot()&&
            target.serial()==actor.serial()&&target.life()==actor.life()&&grant.minecraftUuid().equals(uuid)&&
            actor.minecraftPlayer().equals(uuid);
    }
    private static boolean connectedClient(ServerPlayerEntity player){
        return player!=null&&player.networkHandler!=null&&player.networkHandler.player==player&&
            player.networkHandler.getConnection().isOpen()&&player.networkHandler.hasChannel(HostPayload.ID);
    }
    private static boolean effectClient(ServerPlayerEntity player){
        return connectedClient(player)&&!player.isRemoved();
    }
    private static boolean sameHeldAnchor(HostEffectGrant.Grant a,HostEffectGrant.Grant b){
        int axes=a.snapshot().movement().holdAxes();var x=a.nativeAnchorFeet();var y=b.nativeAnchorFeet();
        return ((axes&1)==0||x.x()==y.x())&&((axes&2)==0||x.y()==y.y())&&((axes&4)==0||x.z()==y.z());
    }
    private static void retireEffects(Player state,boolean reopen){
        HostPlayerEffects.revoke(state.effects);state.effects=null;state.effectsReady=false;state.effectAligned=false;
        var grant=state.grant;
        if(grant!=null){
            var history=EFFECT_STREAMS[(int)grant.snapshot().target().key()-1];
            if(history!=null&&history.stream==grant.stream()){history.retired=true;history.grant=grant;}
            if(reopen){state.reopen=grant;state.nextReopen=0;}
        }
        if(!reopen)state.reopen=null;
        state.grant=null;state.effectPlayer=null;state.effectHandler=null;
    }
    /** Covers the initial placement/ACK interval before a Binding exists. */
    static void effectTeleporting(ServerPlayerEntity player){
        Player state=PLAYERS.get(player.getUuid());
        if(state!=null&&state.grant!=null&&state.effectPlayer==player&&state.effectHandler==player.networkHandler&&
            !state.effectTeleportAllowed)retireEffects(state,true);
    }
    /** UPDATE is the native server's acknowledgement that this exact stream
     * has acquired movement. OPEN still requires placement at native feet. */
    static HostEffectGrant.Grant relocationGrant(ServerPlayerEntity player,HostPlayerEffects.Binding binding){
        Player state=PLAYERS.get(player.getUuid());
        if(state==null||state.effects!=binding||!state.enabled||!state.placed||!state.ready||
            System.nanoTime()-state.lastReady>=1_000_000_000L||state.entity!=player||
            state.effectPlayer!=player||state.effectHandler!=player.networkHandler||!effectClient(player))return null;
        var host=GoldCraft.HOST_WORLD;var actor=host.actor(player.getUuid());var grant=state.grant;
        long generation=GoldCraft.currentAuthorityGeneration(),frequency=GoldCraft.currentAuthorityClockFrequency();
        if(!GoldCraft.effectsNegotiated()||generation==0||generation!=state.effectGeneration||
            generation!=effectGeneration||grant==null||grant.operation()!=HostEffectGrant.UPDATE||
            grant.serviceGeneration()!=effectServiceGeneration||grant.nativeConnection()!=effectNativeConnection||
            frequency<=0||frequency!=grant.frequency()||state.epoch!=host.epoch()||
            !effectTarget(grant,host,actor,player.getUuid())||!hostAlive(actor)||!actor.minecraftForm()||
            state.serial!=actor.serial()||state.life!=actor.life()||player.getWorld()!=hostWorld(player.getServer(),host))return null;
        return grant;
    }
    static void effectCorrection(ServerPlayerEntity player,Runnable correction){
        Player state=PLAYERS.get(player.getUuid());
        if(state==null||state.effectPlayer!=player||state.effectHandler!=player.networkHandler){correction.run();return;}
        boolean previous=state.effectTeleportAllowed;state.effectTeleportAllowed=true;
        try{correction.run();}finally{state.effectTeleportAllowed=previous;}
    }
    /** Main-thread ingress after GoldCraft authenticated the exact link generation.
     * Store only the latest publication for one exact life; never bind on receipt. */
    public static void effectGrant(MinecraftServer server,HostWorldState host,HostEffectGrant.Grant grant){
        if(!server.isOnThread())throw new IllegalStateException("Server thread required");
        if(grant==null||!GoldCraft.effectsNegotiated())return;
        long generation=GoldCraft.currentAuthorityGeneration(),frequency=GoldCraft.currentAuthorityClockFrequency();
        if(generation==0||frequency<=0||frequency!=grant.frequency())return;
        UUID uuid=grant.minecraftUuid();Player state=PLAYERS.get(uuid);var actor=host.actor(uuid);
        var player=server.getPlayerManager().getPlayer(uuid);
        if(state==null||state.epoch!=host.epoch()||!effectTarget(grant,host,actor,uuid)||
            !hostAlive(actor)||!actor.minecraftForm()||!effectClient(player)||
            (grant.snapshot().movement().capability()&~HostEffects.POSITION_HOLD)!=0)return;
        var now=LocalQpcClock.system().read();
        if(now==null||!grant.lease().liveAt(now))return;
        if(effectGeneration==0){
            effectGeneration=generation;effectServiceGeneration=grant.serviceGeneration();effectNativeConnection=grant.nativeConnection();
        }
        if(effectGeneration!=generation||effectServiceGeneration!=grant.serviceGeneration()||effectNativeConnection!=grant.nativeConnection())return;
        // Expiration/foreign teleport may happen between ticks. Retire before
        // considering a repeated OPEN, not after replacing its expired lease.
        if(state.grant!=null&&(!state.grant.lease().liveAt(now)||
            state.effects!=null&&HostPlayerEffects.readiness(state.effects,frequency)==null))retireEffects(state,true);
        int slot=actor.slot()-1;StreamHistory history=EFFECT_STREAMS[slot];
        if(history==null){history=new StreamHistory();EFFECT_STREAMS[slot]=history;}
        int order=Long.compareUnsigned(grant.stream(),history.stream);
        if(order<0||order==0&&(history.retired||!grant.snapshot().target().equals(history.target)||!uuid.equals(history.uuid)))return;
        if(order>0){
            if(grant.operation()!=HostEffectGrant.OPEN)return;
            retireEffects(state,false);
            history.stream=grant.stream();history.target=grant.snapshot().target();history.uuid=uuid;history.retired=false;
            state.effectGeneration=generation;state.effectPlayer=player;state.effectHandler=player.networkHandler;
        }else{
            var previous=state.grant;
            if(previous==null||previous.stream()!=grant.stream()||state.effectGeneration!=generation||
                state.effectPlayer!=player||state.effectHandler!=player.networkHandler||
                Long.compareUnsigned(grant.publication(),previous.publication())<=0||
                Long.compareUnsigned(grant.snapshot().revision(),previous.snapshot().revision())<0)return;
            if(grant.snapshot().revision()==previous.snapshot().revision()&&
                (!grant.snapshot().equals(previous.snapshot())||!sameHeldAnchor(previous,grant)))return;
        }
        state.grant=grant;history.grant=grant;
    }
    private static Vec3d minecraftFeet(HostEffectGrant.Point nativeFeet){
        return new Vec3d(nativeFeet.x()/Wire.UNITS_PER_BLOCK,
            nativeFeet.z()/Wire.UNITS_PER_BLOCK+Wire.Y_OFFSET,-nativeFeet.y()/Wire.UNITS_PER_BLOCK);
    }
    private static Vec3d effectFeet(HostEffectGrant.Grant grant){
        var feet=grant.nativeFeet();var anchor=grant.nativeAnchorFeet();int axes=grant.snapshot().movement().holdAxes();
        return new Vec3d(((axes&1)!=0?anchor.x():feet.x())/Wire.UNITS_PER_BLOCK,
            ((axes&4)!=0?anchor.z():feet.z())/Wire.UNITS_PER_BLOCK+Wire.Y_OFFSET,
            -((axes&2)!=0?anchor.y():feet.y())/Wire.UNITS_PER_BLOCK);
    }
    private static void requestReopen(Player state,HostWorldState host,HostWorldState.Actor actor,ServerPlayerEntity player){
        var grant=state.reopen;
        if(grant==null){
            var history=EFFECT_STREAMS[actor.slot()-1];
            if(history==null||!history.retired||!effectTarget(history.grant,host,actor,player.getUuid()))return;
            // A Minecraft-only reconnect/respawn can preserve the native life.
            // Recover with a new stream, never by reinstating the saved token.
            grant=history.grant;state.reopen=grant;state.effectGeneration=effectGeneration;
        }
        if(!effectTarget(grant,host,actor,player.getUuid())||!hostAlive(actor)||!actor.minecraftForm()||
            !effectClient(player)||state.effectGeneration!=GoldCraft.currentAuthorityGeneration()){
            state.reopen=null;return;
        }
        long now=System.nanoTime();
        if(state.nextReopen!=0&&now-state.nextReopen<0)return;
        // Throttle failed queue attempts as well; no peer timestamp is derived
        // from nanoTime. The grant's absolute QPC deadline remains unchanged.
        state.nextReopen=now+REOPEN_INTERVAL_NANOS;
        GoldCraft.sendToHost(Wire.EFFECT_REOPEN,HostEffectGrant.reopen(grant));
    }
    private static HostEffectGrant.Grant updateEffects(Player state,HostWorldState host,HostWorldState.Actor actor,
                                                       ServerPlayerEntity player,ServerWorld world){
        state.effectsReady=false;
        long generation=GoldCraft.currentAuthorityGeneration(),frequency=GoldCraft.currentAuthorityClockFrequency();
        var grant=state.grant;
        if(grant==null){requestReopen(state,host,actor,player);return null;}
        var now=LocalQpcClock.system().read();
        if(generation==0||state.effectGeneration!=generation||frequency<=0||frequency!=grant.frequency()||
            now==null||!grant.lease().liveAt(now)){
            retireEffects(state,true);requestReopen(state,host,actor,player);return null;
        }
        if(!effectTarget(grant,host,actor,player.getUuid())||state.serial!=actor.serial()||state.life!=actor.life()||
            state.entity!=player||state.effectPlayer!=player||state.effectHandler!=player.networkHandler||
            !effectClient(player)||!player.isAlive()||player.getWorld()!=world){
            retireEffects(state,false);return null;
        }
        if(state.effects==null){
            if(!state.placed||!state.ready||System.nanoTime()-state.lastReady>=1_000_000_000L||teleportPending(player))return null;
            if(!state.effectAligned){
                var feet=effectFeet(grant);
                effectCorrection(player,()->player.networkHandler.requestTeleport(feet.x,feet.y,feet.z,player.getYaw(),player.getPitch()));
                player.setVelocity(0,0,0);player.fallDistance=0;state.effectAligned=true;
                return null;
            }
            state.effects=HostPlayerEffects.bind(player,grant.address());
            if(state.effects==null){retireEffects(state,true);requestReopen(state,host,actor,player);return null;}
        }
        var installed=HostPlayerEffects.readiness(state.effects,frequency);
        // A new binding has no snapshot yet. An existing binding must retain
        // its installed token; a retired one can only recover via a new stream.
        if(installed==null||installed.lease().publication()!=grant.publication()){
            if(!HostPlayerEffects.install(state.effects,grant.snapshot(),minecraftFeet(grant.nativeAnchorFeet()),grant.lease())){
                retireEffects(state,true);requestReopen(state,host,actor,player);return null;
            }
        }
        installed=HostPlayerEffects.readiness(state.effects,frequency);
        if(installed==null){retireEffects(state,true);requestReopen(state,host,actor,player);return null;}
        state.effectsReady=installed.ready()&&installed.address().equals(grant.address())&&
            installed.lease().publication()==grant.publication()&&installed.snapshot().equals(grant.snapshot());
        return state.effectsReady?grant:null;
    }
    public static ServerWorld hostWorld(MinecraftServer server,HostWorldState host) {
        if(host.geometry()==null)return null;
        Identifier id=Identifier.of(host.dimension());
        return server.getWorld(RegistryKey.of(RegistryKeys.WORLD,id));
    }
    public static void tick(MinecraftServer server,HostWorldState host) {
        HostPlayerEffects.pump(server);
        boolean effectsNegotiated=GoldCraft.effectsNegotiated();
        ServerWorld world=hostWorld(server,host);
        if(world==null){
            if(effectsNegotiated)for(var state:PLAYERS.values()){
                retireEffects(state,false);state.enabled=state.placed=false;
                if(state.entity!=null){hold(state.entity);SharedVitals.active(state.entity,false);}
            }
            return;
        }
        HostWorldReset.activate(world,host.epoch());
        if(HostCollision.state(world)!=host)HostCollision.attach(world,host);
        for(var entry:PLAYERS.entrySet()) {
            Player state=entry.getValue();var actor=host.actor(entry.getKey());ServerPlayerEntity player=server.getPlayerManager().getPlayer(entry.getKey());
            if(player==null||state.epoch!=host.epoch()){
                if(effectsNegotiated)retireEffects(state,false);
                continue;
            }
            if(actor==null) {
                // A still-connected Minecraft client must not leave a live ghost body
                // after its native CS connection goes away.
                if(state.placed||state.enabled)hold(player);
                state.ready=state.placed=state.alive=state.enabled=false;
                if(effectsNegotiated)retireEffects(state,false);
                SharedVitals.active(player,false);
                continue;
            }
            if(state.serial!=actor.serial()||state.life!=actor.life()||state.entity!=player){
                if(effectsNegotiated&&state.grant!=null&&
                    (!effectTarget(state.grant,host,actor,player.getUuid())||state.effectPlayer!=player))retireEffects(state,false);
                hold(player);state.serial=actor.serial();state.life=actor.life();state.entity=player;
                state.ready=state.placed=state.enabled=false;
            }
            boolean alive=actor.life()!=0&&(actor.flags()&1)!=0&&(actor.flags()&4)==0&&actor.team()!=0&&actor.team()!=3;
            // Vanilla removes a corpse after 20 ticks while its connection
            // stays open. Admit that connection to the native-authorized respawn
            // below; effect grants and control still require the live replacement.
            if(effectsNegotiated&&!connectedClient(player)){
                retireEffects(state,alive&&actor.minecraftForm());state.enabled=false;
                SharedVitals.active(player,false);sendPose(host,actor,player,false,true,null);
                continue;
            }
            if(SharedVitals.prepare(player,host,actor)){
                state.enabled=false;SharedVitals.active(player,false);
                if(effectsNegotiated){retireEffects(state,false);sendPose(host,actor,player,false,true,null);}
                continue;
            }
            boolean wantsControl=actor.minecraftForm()&&state.ready&&System.nanoTime()-state.lastReady<1_000_000_000L;
            if(effectsNegotiated&&(!alive||!actor.minecraftForm()||
                !wantsControl&&(state.enabled||state.effects!=null||state.effectAligned)))
                retireEffects(state,alive&&actor.minecraftForm());
            if((!alive||!wantsControl)&&state.enabled){hold(player);state.placed=state.enabled=false;}
            if(!alive){
                player=HostDeathView.follow(server,world,host,actor,player);
                state.entity=player;state.ready=state.placed=state.alive=state.enabled=false;
                SharedVitals.active(player,false);
                sendPose(host,actor,player,false,effectsNegotiated,null);
                continue;
            }
            if(alive&&(!state.placed||!player.isAlive()||player.getWorld()!=world)) {
                if(effectsNegotiated&&state.effects!=null)retireEffects(state,true);
                hold(player);
                if(!player.isAlive()) {
                    player=server.getPlayerManager().respawnPlayer(player,false,Entity.RemovalReason.KILLED);
                    // PlayerManager returns a replacement, but updating the handler
                    // is the caller's responsibility (also done by vanilla respawn).
                    player.networkHandler.player=player;
                    hold(player);
                    SharedVitals.prepare(player,host,actor);
                }
                state.entity=player;state.enabled=false;
                place(player,world,actor);state.placed=true;
                GoldCraft.LOGGER.info("Placed paired MC player: slot={} serial={} life={} dimension={} position={}",actor.slot(),actor.serial(),actor.life(),world.getRegistryKey().getValue(),player.getPos());
            }
            if(alive&&!actor.minecraftForm()){
                // The Minecraft player becomes a non-colliding, invisible spectator
                // attached to its native owner. The ordinary CS body stays authoritative.
                hold(player);state.ready=state.enabled=false;
                var feet=actor.minecraftFeet();
                if(!teleportPending(player)&&(player.squaredDistanceTo(feet.x(),feet.y(),feet.z())>0.0001
                    ||Math.abs(net.minecraft.util.math.MathHelper.wrapDegrees(player.getYaw()+actor.yaw()+90))>0.5
                    ||Math.abs(player.getPitch()-actor.pitch())>0.5))place(player,world,actor);
                state.alive=alive;
                SharedVitals.active(player,false);
                if(effectsNegotiated)sendPose(host,actor,player,false,true,null);
                continue;
            }
            wantsControl=actor.minecraftForm()&&state.ready&&System.nanoTime()-state.lastReady<1_000_000_000L;
            if(alive&&state.placed&&wantsControl&&!state.enabled&&!teleportPending(player)&&player.networkHandler.player==player) {
                var feet=actor.minecraftFeet();
                // Native walking/gravity may have moved the spawn while waiting.
                // Complete the vanilla teleport acknowledgement before leasing it.
                if(!effectsNegotiated&&(actor.flags()&32)==0&&player.squaredDistanceTo(feet.x(),feet.y(),feet.z())>0.00390625){
                    place(player,world,actor);
                }else{
                    release(player);player.networkHandler.syncWithPlayerPosition();state.enabled=true;
                }
            }
            state.alive=alive;
            boolean enabled=state.enabled&&wantsControl&&state.placed&&alive&&player.isAlive()&&player.getWorld()==world&&player.networkHandler.player==player;
            if(effectsNegotiated)enabled=enabled&&effectClient(player);
            SharedVitals.active(player,enabled);
            HostEffectGrant.Grant proof=null;
            if(effectsNegotiated){
                if(enabled){
                    proof=updateEffects(state,host,actor,player,world);
                    // Missing lease/ACK readiness must not manufacture a form
                    // release. Actual ownership failures take the disabled path.
                    if(proof==null)continue;
                }else{
                    state.effectsReady=false;
                    if(state.effects!=null)retireEffects(state,alive&&actor.minecraftForm());
                }
            }
            sendPose(host,actor,player,enabled,effectsNegotiated,proof);
        }
        writeDiagnostics(server,host);
    }
    private static void sendPose(HostWorldState host,HostWorldState.Actor actor,ServerPlayerEntity player,
                                 boolean enabled,boolean negotiated,HostEffectGrant.Grant proof){
        // Exhaustion is fail-closed; never wrap a timeline and publish old poses.
        if(negotiated&&poseSequence==-1L)return;
        int flags=negotiated&&!enabled?0:(enabled?1:0)|(player.isOnGround()?2:0)|(player.isSneaking()?4:0);
        var pos=player.getPos();var velocity=player.getVelocity();
        Wire.Writer w=negotiated?HostEffectGrant.posePrefix(proof):new Wire.Writer();
        w.i64(host.epoch()).i64(++poseSequence).i32(actor.slot()).i32(actor.serial()).i32(actor.life()).bytes(Wire.uuid(player.getUuid())).i32(flags)
            .f32((float)pos.x).f32((float)pos.y).f32((float)pos.z).f32((float)velocity.x).f32((float)velocity.y).f32((float)velocity.z)
            .f32(player.getPitch()).f32(player.getYaw()).f32(player.getStandingEyeHeight()).f32(player.getWidth()).f32(player.getHeight());
        GoldCraft.sendToHost(negotiated?Wire.EFFECT_POSE:Wire.AUTHORITATIVE_POSE,w.toByteArray());
    }
    private static void writeDiagnostics(MinecraftServer server,HostWorldState host){
        String path=System.getenv("GOLDCRAFT_SERVER_STATUS");
        if(path==null||System.nanoTime()-lastDiagnostic<250_000_000L)return;
        lastDiagnostic=System.nanoTime();JsonObject data=new JsonObject();JsonArray players=new JsonArray();
        data.addProperty("world",Long.toUnsignedString(host.epoch()));data.addProperty("map",host.map());
        for(var entry:PLAYERS.entrySet()){
            var player=server.getPlayerManager().getPlayer(entry.getKey());if(player==null)continue;
            var state=entry.getValue();var actor=host.actor(entry.getKey());JsonObject record=new JsonObject();
            record.addProperty("uuid",entry.getKey().toString());record.addProperty("slot",actor==null?0:actor.slot());
            record.addProperty("serial",state.serial);record.addProperty("life",state.life);record.addProperty("entityId",player.getId());
            record.addProperty("handlerMatchesPlayer",player.networkHandler.player==player);record.addProperty("trackedEntityMatchesPlayer",state.entity==player);
            record.addProperty("teleportPending",teleportPending(player));record.addProperty("ready",state.ready);record.addProperty("enabled",state.enabled);
            record.addProperty("effectsReady",state.effectsReady);
            record.addProperty("effectStream",state.grant==null?"0":Long.toUnsignedString(state.grant.stream()));
            record.addProperty("alive",player.isAlive());record.addProperty("health",player.getHealth());record.addProperty("mode",player.interactionManager.getGameMode().getName());
            record.addProperty("removed",player.isRemoved());record.addProperty("deathAwaitingAuthority",SharedVitals.deferRemoval(player));
            record.addProperty("minecraftForm",actor!=null&&actor.minecraftForm());
            record.addProperty("spawn",actor==null?0:actor.spawn());record.addProperty("vitalsAck",actor==null?0:actor.vitalsAck());record.addProperty("vitalsPending",SharedVitals.pending(entry.getKey()));
            record.addProperty("dimension",player.getWorld().getRegistryKey().getValue().toString());
            JsonArray pos=new JsonArray();pos.add(player.getX());pos.add(player.getY());pos.add(player.getZ());record.add("position",pos);players.add(record);
        }
        data.add("players",players);MinecraftObjects.diagnostics(data);MinecraftFluids.diagnostics(data);NativePlayers.diagnostics(data);
        try{
            Path target=Path.of(path).resolveSibling("minecraft-server-status.json"),temporary=target.resolveSibling(target.getFileName()+".tmp");
            Files.writeString(temporary,data.toString());Files.move(temporary,target,StandardCopyOption.REPLACE_EXISTING);
        }catch(IOException ignored){/* Optional diagnostics cannot interrupt the server. */}
    }
}
