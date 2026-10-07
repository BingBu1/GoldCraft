package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.mixin.ServerPlayNetworkHandlerAccessor;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.Identifier;
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
    private static final class Player {
        long epoch,lastReady;
        int serial,life;
        ServerPlayerEntity entity;
        boolean ready,placed,alive,enabled;
    }
    private HostSession(){}
    public static void clear(MinecraftServer server){
        for(var uuid:PLAYERS.keySet()){var player=server.getPlayerManager().getPlayer(uuid);if(player!=null)hold(player);}
        PLAYERS.clear();
    }
    public static void disconnect(ServerPlayerEntity player){release(player);PLAYERS.remove(player.getUuid());SharedVitals.disconnect(player.getUuid());}
    public static void stopped(){PLAYERS.clear();HELD.clear();}
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
    // Called only for a newly accepted binding, including a reused slot in the same map.
    public static void paired(UUID uuid,long epoch){Player p=new Player();p.epoch=epoch;PLAYERS.put(uuid,p);}
    public static void ready(UUID uuid,long epoch,int serial,int life,boolean ready){
        Player p=PLAYERS.get(uuid);
        if(p!=null&&p.epoch==epoch&&p.serial==serial&&p.life==life&&life!=0){p.ready=ready;p.lastReady=System.nanoTime();}
    }
    public static void respawned(ServerPlayerEntity newPlayer){
        Player state=PLAYERS.get(newPlayer.getUuid());if(state==null)return;
        // Vanilla preserves the numeric entity ID across respawn. Track the actual
        // entity instance, and wait until the caller updates networkHandler.player.
        state.entity=newPlayer;state.ready=state.placed=state.enabled=false;hold(newPlayer);
    }
    private static boolean teleportPending(ServerPlayerEntity player){
        return ((ServerPlayNetworkHandlerAccessor)player.networkHandler).goldcraft$requestedTeleportPos()!=null;
    }
    private static void place(ServerPlayerEntity player,ServerWorld world,HostWorldState.Actor actor){
        var feet=actor.minecraftFeet();
        player.teleport(world,feet.x(),feet.y(),feet.z(),-actor.yaw()-90,actor.pitch());
        player.setVelocity(0,0,0);player.fallDistance=0;
        player.networkHandler.syncWithPlayerPosition();
    }
    public static ServerWorld hostWorld(MinecraftServer server,HostWorldState host) {
        if(host.geometry()==null)return null;
        Identifier id=Identifier.of(host.dimension());
        return server.getWorld(RegistryKey.of(RegistryKeys.WORLD,id));
    }
    public static void tick(MinecraftServer server,HostWorldState host) {
        ServerWorld world=hostWorld(server,host);if(world==null)return;
        HostWorldReset.activate(world,host.epoch());
        if(HostCollision.state(world)!=host)HostCollision.attach(world,host);
        for(var entry:PLAYERS.entrySet()) {
            Player state=entry.getValue();var actor=host.actor(entry.getKey());ServerPlayerEntity player=server.getPlayerManager().getPlayer(entry.getKey());
            if(player==null||state.epoch!=host.epoch())continue;
            if(actor==null) {
                // A still-connected Minecraft client must not leave a live ghost body
                // after its native CS connection goes away.
                if(state.placed||state.enabled)hold(player);
                state.ready=state.placed=state.alive=state.enabled=false;
                SharedVitals.active(player,false);
                continue;
            }
            if(state.serial!=actor.serial()||state.life!=actor.life()||state.entity!=player){
                hold(player);state.serial=actor.serial();state.life=actor.life();state.entity=player;
                state.ready=state.placed=state.enabled=false;
            }
            boolean alive=actor.life()!=0&&(actor.flags()&1)!=0&&(actor.flags()&4)==0&&actor.team()!=0&&actor.team()!=3;
            if(SharedVitals.prepare(player,host,actor)){
                state.enabled=false;SharedVitals.active(player,false);
                continue;
            }
            boolean wantsControl=actor.minecraftForm()&&state.ready&&System.nanoTime()-state.lastReady<1_000_000_000L;
            if((!alive||!wantsControl)&&state.enabled){hold(player);state.placed=state.enabled=false;}
            if(!alive){if(state.alive)hold(player);state.placed=false;}
            if(alive&&(!state.placed||!player.isAlive()||player.getWorld()!=world)) {
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
                continue;
            }
            wantsControl=actor.minecraftForm()&&state.ready&&System.nanoTime()-state.lastReady<1_000_000_000L;
            if(alive&&state.placed&&wantsControl&&!state.enabled&&!teleportPending(player)&&player.networkHandler.player==player) {
                var feet=actor.minecraftFeet();
                // Native walking/gravity may have moved the spawn while waiting.
                // Complete the vanilla teleport acknowledgement before leasing it.
                if((actor.flags()&32)==0&&player.squaredDistanceTo(feet.x(),feet.y(),feet.z())>0.00390625){
                    place(player,world,actor);
                }else{
                    release(player);player.networkHandler.syncWithPlayerPosition();state.enabled=true;
                }
            }
            state.alive=alive;
            boolean enabled=state.enabled&&wantsControl&&state.placed&&alive&&player.isAlive()&&player.getWorld()==world&&player.networkHandler.player==player;
            SharedVitals.active(player,enabled);
            int flags=(enabled?1:0)|(player.isOnGround()?2:0)|(player.isSneaking()?4:0);
            var pos=player.getPos();var velocity=player.getVelocity();
            Wire.Writer w=new Wire.Writer().i64(host.epoch()).i64(++poseSequence).i32(actor.slot()).i32(actor.serial()).i32(actor.life()).bytes(Wire.uuid(player.getUuid())).i32(flags)
                .f32((float)pos.x).f32((float)pos.y).f32((float)pos.z).f32((float)velocity.x).f32((float)velocity.y).f32((float)velocity.z)
                .f32(player.getPitch()).f32(player.getYaw()).f32(player.getStandingEyeHeight()).f32(player.getWidth()).f32(player.getHeight());
            GoldCraft.sendToHost(Wire.AUTHORITATIVE_POSE,w.toByteArray());
        }
        writeDiagnostics(server,host);
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
            record.addProperty("alive",player.isAlive());record.addProperty("health",player.getHealth());record.addProperty("mode",player.interactionManager.getGameMode().getName());
            record.addProperty("removed",player.isRemoved());record.addProperty("deathAwaitingAuthority",SharedVitals.deferRemoval(player));
            record.addProperty("minecraftForm",actor!=null&&actor.minecraftForm());
            record.addProperty("spawn",actor==null?0:actor.spawn());record.addProperty("vitalsAck",actor==null?0:actor.vitalsAck());record.addProperty("vitalsPending",SharedVitals.pending(entry.getKey()));
            record.addProperty("dimension",player.getWorld().getRegistryKey().getValue().toString());
            JsonArray pos=new JsonArray();pos.add(player.getX());pos.add(player.getY());pos.add(player.getZ());record.add("position",pos);players.add(record);
        }
        data.add("players",players);MinecraftObjects.diagnostics(data);NativePlayers.diagnostics(data);
        try{
            Path target=Path.of(path).resolveSibling("minecraft-server-status.json"),temporary=target.resolveSibling(target.getFileName()+".tmp");
            Files.writeString(temporary,data.toString());Files.move(temporary,target,StandardCopyOption.REPLACE_EXISTING);
        }catch(IOException ignored){/* Optional diagnostics cannot interrupt the server. */}
    }
}
