package dev.goldcraft.statusdeathprobe;

import com.google.gson.JsonObject;
import com.mojang.authlib.GameProfile;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.BspMap;
import dev.goldcraft.world.NativePlayers;
import dev.goldcraft.world.NativePlayerEntity;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import dev.goldcraft.world.SharedVitals;
import dev.goldcraft.world.HostSession;
import net.minecraft.core.BlockPos;
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
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.monster.Slime;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.level.GameRules;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.AABB;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.living.LivingDeathEvent;
import net.neoforged.neoforge.event.level.ExplosionEvent;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/** Real FML lifecycle/effects; only host snapshots and player connections are fixtures. */
@Mod("goldcraft_status_death_probe")
public final class StatusDeathProbe {
    static final HostWorldState host=GoldCraft.HOST_WORLD;
    static final UUID NONE=new UUID(0,0);
    static final AABB AREA=new AABB(-10,60,-10,10,80,10);
    static MinecraftServer server;static ServerLevel world;
    static int checks,winds,deaths;static long epoch,tick;static byte[] bsp;
    static Entity windSource;static net.minecraft.world.phys.Vec3 windPosition;
    static void check(boolean condition,String message){checks++;if(!condition)throw new AssertionError(message);}
    public StatusDeathProbe(){
        NeoForge.EVENT_BUS.addListener(StatusDeathProbe::run);
        NeoForge.EVENT_BUS.addListener((ExplosionEvent.Detonate e)->{
            winds++;windSource=e.getExplosion().getDirectSourceEntity();windPosition=e.getExplosion().center();
        });
        NeoForge.EVENT_BUS.addListener((LivingDeathEvent e)->{if(e.getEntity() instanceof ServerPlayer)deaths++;});
    }
    static HostWorldState.Actor actor(int serial,int spawn,int life,int team,int flags,UUID uuid,float x){
        return new HostWorldState.Actor(2,serial,42,team,flags,life,(flags&1)!=0?100:1,0,
            new HostWorldState.Vector(x*32,0,228),new HostWorldState.Vector(0,0,0),0,0,
            new HostWorldState.Vector(-16,-16,-36),new HostWorldState.Vector(16,16,36),uuid,spawn,0);
    }
    static void snapshot(HostWorldState.Actor... actors){
        var w=new Wire.Writer().i64(host.epoch()).i64(++tick).f32(0).i32(0).i32(actors.length);
        for(var a:actors){
            w.i32(a.slot()).i32(a.serial()).i32(a.userid()).i32(a.team()).i32(a.flags()).i32(a.life()).f32(a.health()).f32(a.armor());
            for(var v:List.of(a.origin(),a.velocity()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.f32(a.pitch()).f32(a.yaw());
            for(var v:List.of(a.mins(),a.maxs()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.bytes(Wire.uuid(a.minecraftPlayer())).i32(a.spawn()).i64(a.vitalsAck());
        }
        host.actors(w.toByteArray());
    }
    static void fresh()throws Exception{
        NativePlayers.clear();SharedVitals.clear();host.clear();tick=0;winds=deaths=0;windSource=null;
        var stale=new ArrayList<Entity>();world.getAllEntities().forEach(stale::add);
        for(var e:stale)if(e instanceof Slime||e instanceof NativePlayerHitboxEntity||e instanceof ServerPlayer)e.discard();
        host.world(new Wire.Writer().i64(++epoch).string("cs_assault").toByteArray());
        host.bsp(new Wire.Writer().i64(epoch).i32((int)BspMap.checksum(bsp)).i32(bsp.length).bytes(bsp).toByteArray());
        // Resolve the production manager into this real flat test world. The BSP
        // is parsed normally; this fixture does not assert host-floor support.
        var dimension=HostWorldState.class.getDeclaredField("dimension");dimension.setAccessible(true);dimension.set(host,"minecraft:overworld");
        for(int x=-5;x<=5;x++)for(int z=-5;z<=5;z++)for(int y=69;y<=73;y++)
            world.setBlock(new BlockPos(x,y,z),y==69?Blocks.STONE.defaultBlockState():Blocks.AIR.defaultBlockState(),3);
        world.getGameRules().getRule(GameRules.RULE_MOBGRIEFING).set(false,server);
        world.getGameRules().getRule(GameRules.RULE_MAX_ENTITY_CRAMMING).set(24,server);
    }
    static NativePlayerHitboxEntity living(UUID uuid){
        var a=actor(3,7,11,2,uuid.equals(NONE)?1:17,uuid,0);snapshot(a);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive(),"real native proxy created");return box;
    }
    static void effects(NativePlayerHitboxEntity box){
        box.addEffect(new MobEffectInstance(MobEffects.WIND_CHARGED,1200));
        box.addEffect(new MobEffectInstance(MobEffects.WEAVING,1200));
        box.addEffect(new MobEffectInstance(MobEffects.OOZING,1200));
        box.body().getRandom().setSeed(123456);
    }
    static List<Slime> slimes(){return world.getEntitiesOfClass(Slime.class,AREA,e->!e.isRemoved());}
    static int webs(){int n=0;for(int x=-4;x<=4;x++)for(int z=-4;z<=4;z++)for(int y=69;y<=73;y++)
        if(world.getBlockState(new BlockPos(x,y,z)).is(Blocks.COBWEB))n++;return n;}
    static class QuietConnection extends Connection {
        final io.netty.channel.Channel transport=new io.netty.channel.embedded.EmbeddedChannel();
        QuietConnection(){super(PacketFlow.SERVERBOUND);try{var f=Connection.class.getDeclaredField("channel");f.setAccessible(true);f.set(this,transport);}catch(Exception e){throw new RuntimeException(e);}}
        @Override public io.netty.channel.Channel channel(){return transport;}
        @Override public boolean isConnected(){return true;}
        @Override public boolean isMemoryConnection(){return true;}
        @Override public java.net.SocketAddress getRemoteAddress(){return new java.net.InetSocketAddress("127.0.0.1",1);}
        @Override public void send(Packet<?> p){}
        @Override public void send(Packet<?> p,PacketSendListener l){}
        @Override public void send(Packet<?> p,PacketSendListener l,boolean f){}
    }
    static ServerPlayer player(UUID uuid){
        var profile=new GameProfile(uuid,"DeathProbe");var p=new ServerPlayer(server,world,profile,ClientInformation.createDefault());
        p.connection=new ServerGamePacketListenerImpl(server,new QuietConnection(),p,CommonListenerCookie.createInitial(profile,false)){
            @Override public void send(Packet<?> packet){}
            @Override public void send(Packet<?> packet,PacketSendListener listener){}
        };
        p.setPos(0,70,0);world.addNewPlayer(p);return p;
    }
    static void exercise()throws Exception{
        fresh();var box=living(NONE);var body=box.body();effects(box);
        check(winds==0&&slimes().isEmpty()&&webs()==0,"effects do not fire while alive");
        snapshot(actor(3,7,12,2,4,NONE,200));NativePlayers.tick(server,host);
        check(winds==1,"native death invokes one wind explosion");
        check(windSource==body&&Math.abs(windPosition.x())<.01&&Math.abs(windPosition.z())<.01,"wind uses last living body, not spectator coordinates");
        check(slimes().size()==2&&slimes().stream().allMatch(s->s.getSize()==2&&s.distanceToSqr(0,70.5,0)<.01),"oozing creates two real medium slimes at death");
        check(webs()>0&&webs()<=3,"player weaving places actual webs with mobGriefing false");
        check(box.isRemoved()&&body.isRemoved()&&box.getActiveEffects().isEmpty()&&body.getActiveEffects().isEmpty(),"retired proxy effects consumed");
        int beforeWebs=webs();NativePlayers.tick(server,host);NativePlayers.tick(server,host);
        check(winds==1&&slimes().size()==2&&webs()==beforeWebs,"duplicate dead snapshots do not replay removal");
        snapshot(actor(3,8,13,2,1,NONE,0));NativePlayers.tick(server,host);var reborn=NativePlayers.hitbox(host.actors().getFirst());
        check(reborn!=null&&reborn!=box&&reborn.getActiveEffects().isEmpty(),"respawn has a new effect lifetime");
        reborn.addEffect(new MobEffectInstance(MobEffects.OOZING,1200));
        snapshot(actor(3,8,14,2,4,NONE,0));NativePlayers.tick(server,host);
        check(slimes().size()==4,"new spawn can execute its own death once");
        for(String reason:List.of("disconnect","newSerial","newSpawn","spectator","world","form")){
            fresh();box=living(NONE);effects(box);
            switch(reason){
                case "disconnect"->snapshot();
                case "newSerial"->snapshot(actor(4,7,11,2,4,NONE,0));
                case "newSpawn"->snapshot(actor(3,8,11,2,4,NONE,0));
                case "spectator"->snapshot(actor(3,7,12,3,4,NONE,0));
                case "world"->host.world(new Wire.Writer().i64(++epoch).string("cs_assault").toByteArray());
                case "form"->snapshot(actor(3,7,12,2,65,NONE,0));
            }
            NativePlayers.tick(server,host);check(winds==0&&slimes().isEmpty()&&webs()==0,reason+" is not a death");
            if(reason.equals("form"))check(NativePlayers.hitbox(host.actors().getFirst()).hasEffect(MobEffects.OOZING),"form handoff retains effects");
        }
        fresh();box=living(NONE);box.addEffect(new MobEffectInstance(MobEffects.OOZING,1200));
        world.getGameRules().getRule(GameRules.RULE_MAX_ENTITY_CRAMMING).set(1,server);
        var existing=EntityType.SLIME.create(world);existing.setSize(2,true);existing.setPos(0,70,0);world.addFreshEntity(existing);
        snapshot(actor(3,7,12,2,4,NONE,0));NativePlayers.tick(server,host);check(slimes().size()==1,"vanilla cramming limit is preserved");

        fresh();var p=player(new UUID(0,82346));box=living(p.getUUID());effects(box);HostSession.hold(p);
        var original=box.getEffect(MobEffects.OOZING);SharedVitals.prepare(p,host,host.actors().getFirst());
        snapshot(actor(3,7,12,2,20,p.getUUID(),200));var dead=host.actors().getFirst();
        SharedVitals.prepare(p,host,dead);
        check(p.getHealth()==0&&deaths==1,"paired native death invokes actual player death once");
        check(box.getActiveEffects().isEmpty()&&p.getEffect(MobEffects.OOZING)==original,"same effect instance transfers to paired corpse");
        check(winds==0&&slimes().isEmpty(),"paired effects wait for vanilla removal");
        SharedVitals.prepare(p,host,dead);NativePlayers.tick(server,host);
        check(deaths==1&&winds==0&&slimes().isEmpty(),"native proxy cleanup does not double paired death");
        var follow=Class.forName("dev.goldcraft.world.HostDeathView").getDeclaredMethod("follow",
            MinecraftServer.class,ServerLevel.class,HostWorldState.class,HostWorldState.Actor.class,ServerPlayer.class);
        follow.setAccessible(true);
        var viewer=(ServerPlayer)follow.invoke(null,server,world,host,dead,p);
        check(viewer!=p&&viewer.isAlive()&&viewer.isSpectator()&&p.isRemoved(),"real death viewer respawns the corpse through PlayerList");
        check(server.getPlayerList().getPlayer(p.getUUID())==viewer&&viewer.connection.player==viewer,"replacement is registered with the original connection");
        check(winds==1&&windSource==p&&slimes().size()==2&&webs()>0,"real death-view removal executes actual vanilla effects");
        check(Math.abs(windPosition.x())<.01&&Math.abs(windPosition.z())<.01,"paired wind uses corpse location before spectator follow");
        check(viewer.getActiveEffects().isEmpty(),"death effects do not leak into the spectator replacement");
        SharedVitals.prepare(viewer,host,dead);
        check(follow.invoke(null,server,world,host,dead,viewer)==viewer&&deaths==1&&winds==1&&slimes().size()==2,"repeated death-view updates do not replay death or effects");
        p.remove(Entity.RemovalReason.KILLED);check(winds==1&&slimes().size()==2,"second paired removal cannot replay effects");
        check(p.getActiveEffects().isEmpty(),"paired corpse owns no effects after removal");
        // Do not leave a synthetic network player in the fixture's shutdown loop.
        server.getPlayerList().remove(viewer);
    }
    static void run(ServerStartedEvent e){
        var report=new JsonObject();
        try{
            server=e.getServer();world=server.overworld();world.getChunk(0,0);
            bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            exercise();report.addProperty("passed",true);
        }catch(Throwable t){report.addProperty("passed",false);report.addProperty("error",t.toString());t.printStackTrace();}
        finally{
            report.addProperty("checks",checks);report.addProperty("windExplosions",winds);report.addProperty("playerDeaths",deaths);
            try{NativePlayers.clear();SharedVitals.clear();host.clear();Files.writeString(FMLPaths.GAMEDIR.get().resolve("status-death.json"),report.toString());}
            catch(Exception ex){throw new RuntimeException(ex);}
        }
    }
}
