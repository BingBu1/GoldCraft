package dev.goldcraft.nutritionprobe;

import com.google.gson.JsonObject;
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
import net.minecraft.world.Difficulty;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.level.GameRules;
import net.minecraft.world.level.GameType;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.living.LivingHealEvent;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/** Actual FML callbacks; native snapshots, network and damage acknowledgements are fixtures. */
@Mod("goldcraft_native_nutrition_probe")
public final class NativeNutritionProbe {
    static final HostWorldState host=GoldCraft.HOST_WORLD;
    static final UUID NONE=new UUID(0,0);
    static MinecraftServer server;static ServerLevel world;static byte[] bsp;
    static int checks,healthRequests,healEvents;static long epoch,sequence,snapshotTick;
    static float lastAmount;static boolean periodic,environment,healingCanceled;
    static NativePlayerHitboxEntity healingTarget;
    static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    static void near(float actual,float expected,String message){check(Math.abs(actual-expected)<.0001f,message+" ["+actual+" != "+expected+"]");}
    public NativeNutritionProbe(){
        NeoForge.EVENT_BUS.addListener(NativeNutritionProbe::run);
        NeoForge.EVENT_BUS.addListener((LivingHealEvent event)->{
            if(event.getEntity()==healingTarget){healEvents++;if(healingCanceled)event.setCanceled(true);}
        });
    }
    static HostWorldState.Actor actor(int serial,int spawn,int life,int flags,UUID uuid,float health){
        return new HostWorldState.Actor(2,serial,42,2,flags,life,health,0,
            new HostWorldState.Vector(0,0,228),new HostWorldState.Vector(0,0,0),0,0,
            new HostWorldState.Vector(-16,-16,-36),new HostWorldState.Vector(16,16,36),uuid,spawn,0);
    }
    static void snapshot(HostWorldState.Actor... actors){
        var w=new Wire.Writer().i64(host.epoch()).i64(++snapshotTick).f32(0).i32(0).i32(actors.length);
        for(var a:actors){
            w.i32(a.slot()).i32(a.serial()).i32(a.userid()).i32(a.team()).i32(a.flags()).i32(a.life()).f32(a.health()).f32(a.armor());
            for(var v:List.of(a.origin(),a.velocity()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.f32(a.pitch()).f32(a.yaw());
            for(var v:List.of(a.mins(),a.maxs()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.bytes(Wire.uuid(a.minecraftPlayer())).i32(a.spawn()).i64(a.vitalsAck());
        }
        host.actors(w.toByteArray());
    }
    static void clock()throws Exception{
        var field=MinecraftServer.class.getDeclaredField("tickCount");field.setAccessible(true);field.setInt(server,field.getInt(server)+1);
    }
    static void step(NativePlayerHitboxEntity box,int count)throws Exception{for(int i=0;i<count;i++){clock();box.tick();}}
    static void fresh()throws Exception{
        NativePlayers.clear();SharedVitals.clear();host.clear();HostSession.stopped();
        for(var p:List.copyOf(server.getPlayerList().getPlayers()))server.getPlayerList().remove(p);
        var stale=new ArrayList<Entity>();world.getAllEntities().forEach(stale::add);
        for(var e:stale)if(e instanceof NativePlayerHitboxEntity||e instanceof ServerPlayer)e.discard();
        snapshotTick=0;healthRequests=healEvents=0;lastAmount=0;healingTarget=null;healingCanceled=false;
        host.world(new Wire.Writer().i64(++epoch).string("cs_assault").toByteArray());
        host.bsp(new Wire.Writer().i64(epoch).i32((int)BspMap.checksum(bsp)).i32(bsp.length).bytes(bsp).toByteArray());
        var dim=HostWorldState.class.getDeclaredField("dimension");dim.setAccessible(true);dim.set(host,"minecraft:overworld");
        server.setDifficulty(Difficulty.NORMAL,true);
        world.getGameRules().getRule(GameRules.RULE_NATURAL_REGENERATION).set(true,server);
    }
    static NativePlayerHitboxEntity living(UUID uuid,float health)throws Exception{
        var a=actor(3,7,11,uuid.equals(NONE)?1:17,uuid,health);snapshot(a);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive(),"production native body created");
        var sink=NativePlayerEntity.class.getDeclaredField("damageSink");sink.setAccessible(true);
        sink.set(box.body(),(NativePlayerEntity.DamageSink)(target,source,amount)->{
            healthRequests++;lastAmount=amount;
            var scope=NativeStatusExecution.current(target.hitbox());
            periodic=scope!=null&&scope.periodic;environment=scope!=null&&scope.origin==null&&!scope.traceContact;
            target.healthLedger().sent(++sequence,-amount);return true;
        });
        return box;
    }
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
    @SuppressWarnings("unchecked")
    static ServerPlayer player(UUID uuid)throws Exception{
        var profile=new GameProfile(uuid,"NutritionProbe");var p=new ServerPlayer(server,world,profile,ClientInformation.createDefault());
        p.connection=new ServerGamePacketListenerImpl(server,new QuietConnection(),p,CommonListenerCookie.createInitial(profile,false)){
            @Override public void send(Packet<?> packet){}
            @Override public void send(Packet<?> packet,PacketSendListener listener){}
        };
        p.setPos(0,70,0);world.addNewPlayer(p);
        var players=net.minecraft.server.players.PlayerList.class.getDeclaredField("players");players.setAccessible(true);
        ((List<ServerPlayer>)players.get(server.getPlayerList())).add(p);
        var map=net.minecraft.server.players.PlayerList.class.getDeclaredField("playersByUUID");map.setAccessible(true);
        ((Map<UUID,ServerPlayer>)map.get(server.getPlayerList())).put(uuid,p);
        p.setGameMode(GameType.SURVIVAL);return p;
    }
    static void exercise()throws Exception{
        fresh();var box=living(NONE,100);var food=box.body().getFoodData();
        var effect=new MobEffectInstance(MobEffects.HUNGER,2000,1);box.addEffect(effect);step(box,1);
        check(food.getExhaustionLevel()>.009f,"native hunger runs the vanilla Player exhaustion branch");
        near(food.getExhaustionLevel(),.01f,"one hunger callback per tick");
        check(effect.getDuration()==1999&&box.getEffect(MobEffects.HUNGER)==effect,"single original effect timer retained");
        food.setFoodLevel(8);food.setSaturation(0);
        MobEffects.SATURATION.value().applyInstantenousEffect(null,null,box,2,1);
        check(food.getFoodLevel()==11,"splash/instant saturation adds actual nutrition");near(food.getSaturationLevel(),6,"saturation uses vanilla multiplier");
        box.removeAllEffects();box.addEffect(new MobEffectInstance(MobEffects.SATURATION,1,0));step(box,1);
        check(food.getFoodLevel()==12&&!box.hasEffect(MobEffects.SATURATION),"command/drink saturation uses same owner and expires");
        var pig=EntityType.PIG.create(world);MobEffects.HUNGER.value().applyEffectTick(pig,4);MobEffects.SATURATION.value().applyEffectTick(pig,4);
        check(!pig.hasEffect(MobEffects.HUNGER),"non-player effects keep vanilla behavior");

        fresh();var p=player(new UUID(0,9123));var realFood=p.getFoodData();realFood.setFoodLevel(9);realFood.setSaturation(0);
        HostSession.hold(p);box=living(p.getUUID(),100);food=box.body().getFoodData();
        check(food==realFood&&p.getFoodData()==food,"paired native state reuses real player food object");
        box.addEffect(new MobEffectInstance(MobEffects.HUNGER,200,0));step(box,1);
        near(food.getExhaustionLevel(),.005f,"presentation spectator still receives survival hunger");box.removeAllEffects();
        food.setExhaustion(10);clock();p.doTick();near(food.getExhaustionLevel(),10,"parked player actual tick does not consume food");
        box.tick();near(food.getExhaustionLevel(),6,"native manager consumes one exhaustion threshold");
        p.doTick();box.tick();near(food.getExhaustionLevel(),6,"duplicate native/parked ticks cannot double food update");
        snapshot(actor(3,7,12,113,p.getUUID(),100));p.setGameMode(GameType.SURVIVAL);NativePlayers.tick(server,host);
        check(p.getFoodData()==food&&box.isRemoved(),"switch to Minecraft preserves food and retires native body");
        p.doTick();near(food.getExhaustionLevel(),6,"same-frame form switch cannot double food update");
        clock();p.doTick();near(food.getExhaustionLevel(),2,"Minecraft actual player tick resumes vanilla food update");
        p.setHealth(10);food.setFoodLevel(20);food.setSaturation(5);food.setExhaustion(0);
        for(int tick=0;tick<12;tick++){clock();p.doTick();}
        check(p.getHealth()>10,"returning to MC restores vanilla food regeneration");
        snapshot(actor(3,7,13,17,p.getUUID(),100));HostSession.hold(p);NativePlayers.tick(server,host);
        var back=NativePlayers.hitbox(host.actors().getFirst());check(back.body().getFoodData()==food,"switch back retains exact food object");
        var previous=back;food.setExhaustion(3);
        snapshot(actor(3,7,14,17,p.getUUID(),100));NativePlayers.tick(server,host);back=NativePlayers.hitbox(host.actors().getFirst());
        check(back!=previous&&back.body().getFoodData()==food,"same-spawn native life replacement retains nutrition");
        MobEffects.HUNGER.value().applyEffectTick(previous,20);near(food.getExhaustionLevel(),3,"removed old proxy cannot mutate live food state");
        server.getPlayerList().remove(p);NativePlayers.tick(server,host);p=player(new UUID(0,9123));NativePlayers.tick(server,host);
        check(p.getFoodData()==food,"paired reconnect uses surviving native nutrition state");
        fresh();p=player(new UUID(0,9124));p.setGameMode(GameType.CREATIVE);HostSession.hold(p);box=living(p.getUUID(),5);food=box.body().getFoodData();
        food.setFoodLevel(0);food.setSaturation(0);box.addEffect(new MobEffectInstance(MobEffects.HUNGER,200,1));step(box,80);
        near(food.getExhaustionLevel(),0,"saved creative mode ignores hunger exhaustion");check(healthRequests==0,"saved creative mode cannot starve in presentation spectator");

        fresh();box=living(NONE,50);food=box.body().getFoodData();healingTarget=box;step(box,10);
        check(healthRequests==0&&healEvents==0,"CS form cannot heal from full food and saturation");
        near(food.getSaturationLevel(),5,"disabled food healing does not consume saturation");
        near(box.body().getHealth(),10,"unacknowledged healing does not overwrite authoritative native body health");
        near(box.getHealth(),10,"CS form has no pending food heal prediction");
        check(NativeStatusExecution.current(box)==null,"nutrition health scope is closed after ticking");
        fresh();box=living(NONE,50);healingTarget=box;healingCanceled=true;step(box,10);
        check(healEvents==0&&healthRequests==0,"CS food healing does not invoke healing events");
        fresh();box=living(NONE,50);world.getGameRules().getRule(GameRules.RULE_NATURAL_REGENERATION).set(false,server);step(box,100);
        check(healthRequests==0,"naturalRegeneration false suppresses food healing");
        fresh();p=player(new UUID(0,9125));HostSession.hold(p);box=living(p.getUUID(),50);
        server.setDifficulty(Difficulty.PEACEFUL,true);food=box.body().getFoodData();food.setFoodLevel(2);food.setSaturation(0);
        p.setHealth(10);p.tickCount=19;box.tickCount=19;clock();p.doTick();
        check(food.getFoodLevel()==2&&food.getSaturationLevel()==0&&p.getHealth()==10,"parked player does not duplicate peaceful recovery");
        box.tick();check(food.getFoodLevel()==3&&food.getSaturationLevel()==1,"native peaceful nutrition recovery matches vanilla cadence");
        check(healthRequests==0,"peaceful mode cannot regenerate CS health");
        for(var difficulty:List.of(Difficulty.EASY,Difficulty.NORMAL,Difficulty.HARD)){
            fresh();server.setDifficulty(difficulty,true);box=living(NONE,difficulty==Difficulty.EASY?55:10);
            food=box.body().getFoodData();food.setFoodLevel(0);food.setSaturation(0);step(box,160);
            check(healthRequests==(difficulty==Difficulty.HARD?2:1),"starvation difficulty floor: "+difficulty);
            near(lastAmount,1,"vanilla starvation damage amount");check(periodic&&environment,"starvation retains authority scope");
        }
        fresh();box=living(NONE,100);food=box.body().getFoodData();food.setFoodLevel(3);food.setExhaustion(7);
        snapshot(actor(3,8,12,1,NONE,100));NativePlayers.tick(server,host);var reborn=NativePlayers.hitbox(host.actors().getFirst());
        check(reborn.body().getFoodData()!=food&&reborn.body().getFoodData().getFoodLevel()==20,"new native spawn resets food state");
        MobEffects.HUNGER.value().applyEffectTick(box,20);near(food.getExhaustionLevel(),7,"stale spawn callback rejected");
        snapshot();NativePlayers.tick(server,host);MobEffects.HUNGER.value().applyEffectTick(reborn,20);
        near(reborn.body().getFoodData().getExhaustionLevel(),0,"disconnected native body rejects effects");
        fresh();p=player(new UUID(0,9130));p.getFoodData().setFoodLevel(10);p.getFoodData().setSaturation(0);p.getFoodData().setExhaustion(5);
        clock();p.doTick();check(p.getFoodData().getFoodLevel()==9,"ordinary Minecraft player food tick remains vanilla");
    }
    static void run(ServerStartedEvent event){
        var report=new JsonObject();
        try{server=event.getServer();world=server.overworld();world.getChunk(0,0);
            bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));exercise();report.addProperty("passed",true);
        }catch(Throwable t){report.addProperty("passed",false);report.addProperty("error",t.toString());t.printStackTrace();}
        finally{report.addProperty("checks",checks);
            try{NativePlayers.clear();SharedVitals.clear();host.clear();HostSession.stopped();
                for(var p:List.copyOf(server.getPlayerList().getPlayers()))server.getPlayerList().remove(p);
                Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-nutrition.json"),report.toString());
            }catch(Exception e){throw new RuntimeException(e);}
        }
    }
}
