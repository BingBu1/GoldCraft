package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.mixin.ServerPlayNetworkHandlerAccessor;
import net.minecraft.entity.Entity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;

/** Keeps vanilla chunk/entity tracking alive while native CS owns death viewing. */
final class HostDeathView {
    private HostDeathView() {}

    static ServerPlayerEntity follow(MinecraftServer server,ServerWorld world,
                                    HostWorldState host,HostWorldState.Actor actor,ServerPlayerEntity player) {
        if((actor.flags()&1)!=0&&(actor.flags()&4)==0&&(actor.team()==1||actor.team()==2))return player;
        if(player.networkHandler==null||player.networkHandler.player!=player
            ||!player.networkHandler.getConnection().isOpen())return player;
        if(!player.isAlive()||player.isRemoved()) {
            // Death/drop callbacks have already run once in SharedVitals.prepare.
            // A living, invisible spectator can receive vanilla chunks/entities;
            // a removed corpse cannot. It never receives a movement/effect grant.
            var corpse=player;
            player=server.getPlayerManager().respawnPlayer(corpse,false,Entity.RemovalReason.KILLED);
            player.networkHandler.player=player;
            SharedVitals.presentationRespawn(corpse,player,host,actor);
        }
        HostSession.hold(player);
        var feet=actor.minecraftFeet();
        if(((ServerPlayNetworkHandlerAccessor)player.networkHandler).goldcraft$requestedTeleportPos()==null
            &&(player.getWorld()!=world||player.squaredDistanceTo(feet.x(),feet.y(),feet.z())>0.0001)) {
            player.teleport(world,feet.x(),feet.y(),feet.z(),-actor.yaw()-90,actor.pitch());
            player.networkHandler.syncWithPlayerPosition();
            world.getChunkManager().updatePosition(player);
        }
        player.setVelocity(0,0,0);player.fallDistance=0;
        return player;
    }
}
