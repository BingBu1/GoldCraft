package dev.goldcraft.healthprobe;

import com.mojang.authlib.GameProfile;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.*;
import net.minecraft.network.Connection;
import net.minecraft.network.PacketSendListener;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.PacketFlow;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ClientInformation;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.CommonListenerCookie;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.GameRules;
import net.minecraft.world.level.GameType;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/** Real entities and production consumers; native snapshots and client connection are fixtures. */
final class HealthWorldFixture {
    static final HostWorldState host=GoldCraft.HOST_WORLD;
    static final UUID NONE=new UUID(0,0);
    static MinecraftServer server;
    static ServerLevel world;
    static byte[] bsp;
    static long epoch=40,tick;
    static final List<Packet<?>> clientPackets=new ArrayList<>();
    static void cleanup(){
        NativePlayers.clear();SharedVitals.clear();host.clear();HostSession.stopped();
        for(var p:List.copyOf(server.getPlayerList().getPlayers()))server.getPlayerList().remove(p);
    }
    static void fresh()throws Exception {
        cleanup();clientPackets.clear();tick=0;
        var stale=new ArrayList<Entity>();world.getAllEntities().forEach(stale::add);
        for(var e:stale)if(e instanceof NativePlayerHitboxEntity||e instanceof ServerPlayer)e.discard();
        host.world(new Wire.Writer().i64(++epoch).string("cs_assault").toByteArray());
        host.bsp(new Wire.Writer().i64(epoch).i32((int)BspMap.checksum(bsp)).i32(bsp.length).bytes(bsp).toByteArray());
        var dim=HostWorldState.class.getDeclaredField("dimension");dim.setAccessible(true);dim.set(host,"minecraft:overworld");
        world.getGameRules().getRule(GameRules.RULE_NATURAL_REGENERATION).set(false,server);
    }
    static HostWorldState.Actor actor(UUID uuid,int serial,int spawn,int life,int flags,float health,long ack){
        return new HostWorldState.Actor(2,serial,42,2,flags,life,health,0,
            new HostWorldState.Vector(0,0,228),new HostWorldState.Vector(0,0,0),0,0,
            new HostWorldState.Vector(-16,-16,-36),new HostWorldState.Vector(16,16,36),uuid,spawn,ack);
    }
    static void snapshot(HostWorldState.Actor actor,float base){
        var w=new Wire.Writer().i64(host.epoch()).i64(++tick).f32(0).i32(0).i32(actor==null?0:1);
        if(actor!=null){var a=actor;
            w.i32(a.slot()).i32(a.serial()).i32(a.userid()).i32(a.team()).i32(a.flags()).i32(a.life()).f32(a.health()).f32(a.armor());
            for(var v:List.of(a.origin(),a.velocity()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.f32(a.pitch()).f32(a.yaw());
            for(var v:List.of(a.mins(),a.maxs()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.bytes(Wire.uuid(a.minecraftPlayer())).i32(a.spawn()).i64(a.vitalsAck());
        }
        host.actors(w.toByteArray());
        boolean alive=actor!=null&&(actor.flags()&5)==1&&actor.health()>0;
        var limits=new Wire.Writer().i64(host.epoch()).i64(tick).i32(alive?1:0);
        if(alive)limits.i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life()).f32(base).f32(base).i64(tick);
        if(!host.healthLimits().accept(host,limits.toByteArray()))throw new AssertionError("health snapshot rejected");
    }
    static void clock()throws Exception {
        var field=MinecraftServer.class.getDeclaredField("tickCount");field.setAccessible(true);field.setInt(server,field.getInt(server)+1);
    }
    static void step(NativePlayerHitboxEntity box,int count)throws Exception {for(int i=0;i<count;i++){clock();box.tick();}}
    static class QuietConnection extends Connection {
        final io.netty.channel.Channel transport=new io.netty.channel.embedded.EmbeddedChannel();
        QuietConnection(){super(PacketFlow.SERVERBOUND);try{var f=Connection.class.getDeclaredField("channel");f.setAccessible(true);f.set(this,transport);}catch(Exception e){throw new RuntimeException(e);}}
        @Override public io.netty.channel.Channel channel(){return transport;}
        @Override public boolean isConnected(){return true;}
        @Override public boolean isMemoryConnection(){return true;}
        @Override public java.net.SocketAddress getRemoteAddress(){return new java.net.InetSocketAddress("127.0.0.1",1);}
        @Override public void send(Packet<?> p){clientPackets.add(p);}
        @Override public void send(Packet<?> p,PacketSendListener l){send(p);}
        @Override public void send(Packet<?> p,PacketSendListener l,boolean f){send(p);}
    }
    @SuppressWarnings("unchecked")
    static ServerPlayer player(UUID uuid)throws Exception {
        var profile=new GameProfile(uuid,"HealthProbe");var p=new ServerPlayer(server,world,profile,ClientInformation.createDefault());
        p.connection=new ServerGamePacketListenerImpl(server,new QuietConnection(),p,CommonListenerCookie.createInitial(profile,false)){
            @Override public void send(Packet<?> packet){clientPackets.add(packet);}
            @Override public void send(Packet<?> packet,PacketSendListener listener){send(packet);}
        };
        p.setPos(0,70,0);world.addNewPlayer(p);
        var players=net.minecraft.server.players.PlayerList.class.getDeclaredField("players");players.setAccessible(true);
        ((List<ServerPlayer>)players.get(server.getPlayerList())).add(p);
        var map=net.minecraft.server.players.PlayerList.class.getDeclaredField("playersByUUID");map.setAccessible(true);
        ((Map<UUID,ServerPlayer>)map.get(server.getPlayerList())).put(uuid,p);
        p.setGameMode(GameType.SURVIVAL);
        // The fixture starts at ServerStarted, before a new player's normal
        // spawn protection has ticked down. Exercise damage after that period.
        var protection=ServerPlayer.class.getDeclaredField("spawnInvulnerableTime");protection.setAccessible(true);protection.setInt(p,0);
        return p;
    }
}
