package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.world.ServerWorld;

public final class DeathViewProbe {
    private static int checks;
    private static void check(boolean condition){checks++;if(!condition)throw new AssertionError("check "+checks);}
    private static HostWorldState.Actor actor(ServerPlayerEntity p,int spawn,int flags,float x){
        return new HostWorldState.Actor(2,3,spawn,spawn,flags,2,(flags&1)!=0?100:1,p.uuid,new HostWorldState.Point(x,64,0));
    }
    public static void main(String[] args){
        var server=new MinecraftServer();var world=new ServerWorld();var host=new HostWorldState();GoldCraft.HOST_WORLD=host;
        var player=new ServerPlayerEntity();player.world=world;server.players.put(player.uuid,player);
        var alive=actor(player,4,1,0);host.actor=alive;SharedVitals.prepare(player,host,alive);SharedVitals.active(player,true);
        check(HostDeathView.follow(server,world,host,alive,player)==player);check(server.respawns==0);
        var dead=actor(player,4,0,100);host.actor=dead;SharedVitals.prepare(player,host,dead);
        check(player.deaths==1&&player.drops==4&&player.inventory==0&&player.health==0);
        var corpse=player;player=HostDeathView.follow(server,world,host,dead,player);
        check(player!=corpse&&server.respawns==1&&player.networkHandler.player==player);
        check(player.isAlive()&&player.spectator&&player.x==100&&world.trackingUpdates==1);
        check(player.inventory==0&&player.deaths==1&&player.drops==4);
        // No native resurrection, extra drops, damage deltas or repeated MC respawns.
        for(int tick=0;tick<2400;tick++){
            SharedVitals.prepare(player,host,dead);SharedVitals.capture(server,host);
            check(HostDeathView.follow(server,world,host,dead,player)==player);
            check(server.respawns==1&&player.deaths==1&&player.drops==4&&GoldCraft.messages==0);
        }
        var moved=actor(player,4,0,220);host.actor=moved;
        HostDeathView.follow(server,world,host,moved,player);check(player.x==100); // Old teleport still awaits ACK.
        player.networkHandler.pending=null;HostDeathView.follow(server,world,host,moved,player);
        check(player.x==220&&world.trackingUpdates==2&&server.respawns==1);
        player.networkHandler.pending=null;player.networkHandler.connection.open=false;
        HostDeathView.follow(server,world,host,dead,player);check(player.x==220);
        player.networkHandler.connection.open=true;player.networkHandler.player=corpse;
        HostDeathView.follow(server,world,host,dead,player);check(player.x==220);player.networkHandler.player=player;
        // Native respawn releases the death latch on this presentation body.
        var reborn=actor(player,5,1,220);host.actor=reborn;SharedVitals.prepare(player,host,reborn);
        SharedVitals.active(player,true);check(player.getHealth()==20);
        player.health=15;SharedVitals.capture(server,host);check(GoldCraft.messages==1);
        var second=actor(player,5,0,0);host.actor=second;SharedVitals.prepare(player,host,second);
        check(player.deaths==2&&player.drops==4);player.removed=true;
        var next=HostDeathView.follow(server,world,host,second,player);
        check(next!=player&&server.respawns==2&&next.deaths==2&&next.inventory==0);
        SharedVitals.prepare(next,host,second);check(next.isAlive()&&next.deaths==2);
        SharedVitals.disconnect(next.uuid);check(!SharedVitals.deferDeath(next,next.getDamageSources().genericKill()));
        System.out.println(checks+" production death-view/vitals checks passed");
    }
}
