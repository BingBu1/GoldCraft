package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.world.*;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.Difficulty;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.village.poi.PoiTypes;
import net.minecraft.world.entity.raid.Raid;
import net.minecraft.world.entity.raid.Raider;
import net.minecraft.world.level.GameRules;
import net.minecraft.world.level.GameType;
import net.minecraft.stats.Stats;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.*;

/** Actual omen ticks, occupied POIs, raid waves and deaths; host snapshots are fixtures. */
@Mod("goldcraft_native_raids_probe")
@SuppressWarnings("unchecked")
public final class NativeRaidsProbe {
    static int checks,spawned,killed;
    static final BlockPos HOME=new BlockPos(2,70,0),HOME2=new BlockPos(6,70,0);
    public NativeRaidsProbe(){NeoForge.EVENT_BUS.addListener(NativeRaidsProbe::run);}
    static void check(boolean condition,String label){checks++;if(!condition)throw new AssertionError(label);}
    static void field(Object o,String n,Object v)throws Exception {var f=o.getClass().getDeclaredField(n);f.setAccessible(true);f.set(o,v);}
    static Object field(Object o,String n)throws Exception {var f=o.getClass().getDeclaredField(n);f.setAccessible(true);return f.get(o);}
    static void effects(LivingEntity p,int n)throws Exception {
        var m=LivingEntity.class.getDeclaredMethod("tickEffects");m.setAccessible(true);
        for(int i=0;i<n;i++){clock();m.invoke(p);}
    }
    static void freshRaid()throws Exception {
        fixtureReset();fresh();
        for(var pos:List.of(HOME,HOME2))if(world.getPoiManager().getType(pos).isPresent())world.getPoiManager().remove(pos);
        world.getPoiManager().tick(()->true);
        server.setDifficulty(Difficulty.NORMAL,true);
        world.getGameRules().getRule(GameRules.RULE_DISABLE_RAIDS).set(false,server);
        var stale=new ArrayList<Raider>();world.getAllEntities().forEach(e->{if(e instanceof Raider r)stale.add(r);});
        stale.forEach(Raider::discard);
    }
    static void fixtureReset()throws Exception {
        var raids=world.getRaids();var map=(Map<?,?>)field(raids,"raidMap");
        for(var raid:map.values())((Raid)raid).stop();map.clear();raids.setDirty();
    }
    static void resetProduction()throws Exception {
        Class.forName("dev.goldcraft.world.NativeRaids").getMethod("resetWorld",net.minecraft.server.level.ServerLevel.class).invoke(null,world);
    }
    static void village(){
        var poi=world.getPoiManager();var type=world.registryAccess().registryOrThrow(Registries.POINT_OF_INTEREST_TYPE).getHolderOrThrow(PoiTypes.HOME);
        poi.add(HOME,type);poi.add(HOME2,type);
        check(poi.take(t->t.is(PoiTypes.HOME),(t,p)->p.equals(HOME),HOME,1).isPresent(),"first real POI occupied");
        check(poi.take(t->t.is(PoiTypes.HOME),(t,p)->p.equals(HOME2),HOME2,1).isPresent(),"second real POI occupied");
        poi.tick(()->true);
        check(world.isVillage(new BlockPos(0,70,0)),"actual occupied POI distance identifies village");
    }
    static NativePlayerHitboxEntity nativePlayer() {
        var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);return NativePlayers.hitbox(a);
    }
    static void convert(NativePlayerHitboxEntity box,int amp)throws Exception {
        box.addEffect(new MobEffectInstance(MobEffects.BAD_OMEN,10000,amp));step(box,2);
        check(!box.hasEffect(MobEffects.BAD_OMEN)&&box.hasEffect(MobEffects.RAID_OMEN),"actual effect tick converts native bad omen in village");
        check(box.getEffect(MobEffects.RAID_OMEN).getAmplifier()==amp,"raid omen amplifier preserved");
        check(box.getEffect(MobEffects.RAID_OMEN).getDuration()>=598&&box.getEffect(MobEffects.RAID_OMEN).getDuration()<=600,"original 600 tick delay retained");
    }
    static Raid finishOmen(NativePlayerHitboxEntity box)throws Exception {
        int remaining=box.getEffect(MobEffects.RAID_OMEN).getDuration();step(box,remaining-1);
        check(world.getRaidAt(HOME)==null,"raid does not start before last omen tick");
        step(box,1);var raid=world.getRaidAt(HOME);
        check(raid!=null&&!box.hasEffect(MobEffects.RAID_OMEN),"last omen tick registers real raid exactly once");return raid;
    }
    static void rules()throws Exception {
        freshRaid();var b=nativePlayer();b.addEffect(new MobEffectInstance(MobEffects.BAD_OMEN,1000,1));step(b,3);
        check(b.hasEffect(MobEffects.BAD_OMEN)&&!b.hasEffect(MobEffects.RAID_OMEN),"no occupied village leaves bad omen intact");
        village();server.setDifficulty(Difficulty.PEACEFUL,true);step(b,3);
        check(b.hasEffect(MobEffects.BAD_OMEN)&&!b.hasEffect(MobEffects.RAID_OMEN),"peaceful difficulty blocks conversion");
        server.setDifficulty(Difficulty.NORMAL,true);step(b,2);
        check(b.hasEffect(MobEffects.RAID_OMEN),"conversion resumes after peaceful difficulty ends");
        b.removeEffect(MobEffects.RAID_OMEN);step(b,610);
        check(world.getRaidAt(HOME)==null,"cured raid omen never starts a raid");
        b.addEffect(new MobEffectInstance(MobEffects.RAID_OMEN,1));step(b,1);
        check(world.getRaidAt(HOME)==null,"direct omen without captured village position cannot reuse cured position");
        convert(b,2);world.getGameRules().getRule(GameRules.RULE_DISABLE_RAIDS).set(true,server);
        step(b,b.getEffect(MobEffects.RAID_OMEN).getDuration());
        check(world.getRaidAt(HOME)==null&&!b.hasEffect(MobEffects.RAID_OMEN),"disabled raids consume countdown without creating raid");
        world.getGameRules().getRule(GameRules.RULE_DISABLE_RAIDS).set(false,server);
        convert(b,2);var raid=finishOmen(b);
        check(raid.getRaidOmenLevel()==3&&raid.getCenter().equals(new BlockPos(4,70,0)),"real manager uses level and mean occupied POI center");
        check(world.getRaids().get(raid.getId())==raid,"actual manager owns raid identity");
        step(b,20);check(raid.getRaidOmenLevel()==3,"expired omen never reapplies raid strength");
        raid.setRaidOmenLevel(5);b.addEffect(new MobEffectInstance(MobEffects.BAD_OMEN,1000,4));step(b,3);
        check(b.hasEffect(MobEffects.BAD_OMEN)&&!b.hasEffect(MobEffects.RAID_OMEN),"maximum existing raid blocks new conversion");
        world.getGameRules().getRule(GameRules.RULE_DISABLE_RAIDS).set(true,server);world.getRaids().tick();
        check(raid.isStopped()&&world.getRaids().get(raid.getId())==null,"vanilla gamerule stops and removes active raid");
    }
    static void lifecycle()throws Exception {
        freshRaid();village();var p=player(new UUID(0,916123));p.setGameMode(GameType.SPECTATOR);
        var a=actor(p.getUUID(),3,7,11,17,100,0);snapshot(a,100);NativePlayers.tick(server,host);var b=NativePlayers.hitbox(a);convert(b,1);
        var effect=b.getEffect(MobEffects.RAID_OMEN);step(b,100);int remaining=effect.getDuration();
        p.setGameMode(GameType.SURVIVAL);snapshot(actor(p.getUUID(),3,7,12,113,100,0),100);NativePlayers.tick(server,host);
        check(p.getEffect(MobEffects.RAID_OMEN)==effect&&p.getRaidOmenPosition().equals(new BlockPos(0,70,0)),"native to MC transfers original effect and captured origin");
        effects(p,10);check(effect.getDuration()==remaining-10,"real player resumes same countdown once");
        p.setGameMode(GameType.SPECTATOR);snapshot(actor(p.getUUID(),3,7,13,17,100,0),100);NativePlayers.tick(server,host);b=NativePlayers.hitbox(host.actors().getFirst());
        check(b.getEffect(MobEffects.RAID_OMEN)==effect&&p.getRaidOmenPosition()==null,"return to native moves countdown origin off parked player");
        int before=p.getStats().getValue(Stats.CUSTOM.get(Stats.RAID_TRIGGER));var raid=finishOmen(b);
        check(p.getStats().getValue(Stats.CUSTOM.get(Stats.RAID_TRIGGER))==before+1,"paired account receives one vanilla raid trigger stat");
        resetProduction();check(raid.isStopped()&&world.getRaidAt(HOME)==null,"map raid reset invalidates and removes actual manager entry");
        convert(b,2);snapshot(actor(p.getUUID(),3,8,14,17,100,0),100);NativePlayers.tick(server,host);var respawn=NativePlayers.hitbox(host.actors().getFirst());
        check(!respawn.hasEffect(MobEffects.RAID_OMEN),"new native spawn cannot inherit countdown");
        respawn.addEffect(new MobEffectInstance(MobEffects.RAID_OMEN,1));step(respawn,1);check(world.getRaidAt(HOME)==null,"new spawn cannot inherit origin");
        convert(respawn,2);resetProduction();step(respawn,respawn.getEffect(MobEffects.RAID_OMEN).getDuration());
        check(world.getRaidAt(HOME)==null,"map reset clears still-running native raid countdown origin");
    }
    static void incoming()throws Exception {
        freshRaid();village();var p=player(new UUID(0,916124));
        snapshot(actor(p.getUUID(),3,7,11,113,100,0),100);NativePlayers.tick(server,host);
        p.addEffect(new MobEffectInstance(MobEffects.BAD_OMEN,10000,2));effects(p,2);
        check(p.hasEffect(MobEffects.RAID_OMEN)&&p.getRaidOmenPosition()!=null,"unaltered real player conversion captures village origin");
        var effect=p.getEffect(MobEffects.RAID_OMEN);p.setGameMode(GameType.SPECTATOR);
        snapshot(actor(p.getUUID(),3,7,12,17,100,0),100);NativePlayers.tick(server,host);var b=NativePlayers.hitbox(host.actors().getFirst());
        check(b.getEffect(MobEffects.RAID_OMEN)==effect&&p.getRaidOmenPosition()==null,"MC-created countdown transfers into native form");
        step(b,10);int time=effect.getDuration();var old=b;
        snapshot(actor(p.getUUID(),3,7,13,17,100,0),100);NativePlayers.tick(server,host);b=NativePlayers.hitbox(host.actors().getFirst());
        check(old.isRemoved()&&b!=old&&b.getEffect(MobEffects.RAID_OMEN)==effect&&effect.getDuration()==time,"same-spawn native life replacement preserves original countdown");
        var raid=finishOmen(b);check(raid.getRaidOmenLevel()==3,"MC-origin countdown starts native raid at original level");
        resetProduction();convert(b,1);
        // New authoritative snapshot invalidates the old body before its next
        // ordinary native synchronization tick has removed that object.
        snapshot(actor(p.getUUID(),4,7,14,17,100,0),100);
        step(b,b.getEffect(MobEffects.RAID_OMEN).getDuration());
        check(world.getRaidAt(HOME)==null,"old connection cannot trigger raid before body retirement");
        NativePlayers.tick(server,host);b=NativePlayers.hitbox(host.actors().getFirst());convert(b,1);
        p.setGameMode(GameType.SURVIVAL);snapshot(actor(p.getUUID(),4,7,15,113,100,0),100);NativePlayers.tick(server,host);
        effects(p,p.getEffect(MobEffects.RAID_OMEN).getDuration());raid=world.getRaidAt(HOME);
        check(raid!=null&&raid.getRaidOmenLevel()==2&&p.getRaidOmenPosition()==null,"native-created countdown completes through unaltered real player callback");
    }
    static void actors(List<HostWorldState.Actor> values){
        var w=new Wire.Writer().i64(host.epoch()).i64(++tick).f32(0).i32(0).i32(values.size());
        for(var a:values){
            w.i32(a.slot()).i32(a.serial()).i32(a.userid()).i32(a.team()).i32(a.flags()).i32(a.life()).f32(a.health()).f32(a.armor());
            for(var v:List.of(a.origin(),a.velocity()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.f32(a.pitch()).f32(a.yaw());for(var v:List.of(a.mins(),a.maxs()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.bytes(Wire.uuid(a.minecraftPlayer())).i32(a.spawn()).i64(a.vitalsAck());
        }
        host.actors(w.toByteArray());NativePlayers.tick(server,host);
    }
    static void multiple()throws Exception {
        freshRaid();village();var a=actor(NONE,3,7,11,1,100,0);
        var second=new HostWorldState.Actor(3,8,43,1,1,11,100,0,new HostWorldState.Vector(64,0,228),a.velocity(),0,0,a.mins(),a.maxs(),NONE,7,0);
        actors(List.of(a,second));var first=NativePlayers.hitbox(a);var other=NativePlayers.hitbox(second);
        convert(first,1);check(!other.hasEffect(MobEffects.RAID_OMEN),"two native players do not share omen");
        convert(other,3);var raid=finishOmen(first);
        step(other,other.getEffect(MobEffects.RAID_OMEN).getDuration());
        check(world.getRaidAt(HOME)==raid&&raid.getRaidOmenLevel()==5,"second player extends same real raid with vanilla level cap");
        step(first,10);step(other,10);check(raid.getRaidOmenLevel()==5,"both consumed countdowns stay consumed");
    }
    static void noRaidsDimension()throws Exception {
        freshRaid();var original=world;world=server.getLevel(net.minecraft.world.level.Level.NETHER);world.getChunk(0,0);
        field(host,"dimension","minecraft:the_nether");village();var b=nativePlayer();convert(b,1);
        step(b,b.getEffect(MobEffects.RAID_OMEN).getDuration());
        check(world.getRaidAt(HOME)==null&&!b.hasEffect(MobEffects.RAID_OMEN),"dimension without raids consumes countdown without starting raid");
        cleanup();world=original;
    }
    static void waveAndVictory(boolean paired)throws Exception {
        freshRaid();village();net.minecraft.server.level.ServerPlayer account=null;NativePlayerHitboxEntity b;
        if(paired){
            account=player(new UUID(0,916125));account.setGameMode(GameType.SPECTATOR);
            var a=actor(account.getUUID(),3,7,11,17,100,0);snapshot(a,100);NativePlayers.tick(server,host);b=NativePlayers.hitbox(a);
        }else b=nativePlayer();
        int wins=account==null?0:account.getStats().getValue(Stats.CUSTOM.get(Stats.RAID_WIN));
        convert(b,2);var raid=finishOmen(b);
        // Skip preparation delay only. Vanilla spawnGroup creates all raiders.
        field(raid,"raidCooldownTicks",0);field(raid,"waveSpawnPos",Optional.of(new BlockPos(10,70,0)));raid.tick();
        var raiders=raid.getAllRaiders();spawned+=raiders.size();
        check(raid.hasFirstWaveSpawned()&&!raiders.isEmpty(),"original raid tick creates real first wave");
        for(var mob:List.copyOf(raiders)){
            check(mob.getCurrentRaid()==raid&&mob.level()==world,"real raider belongs to actual raid");
            check(mob.hurt(world.damageSources().playerAttack(b.body()),10000),"native logical player actually damages raider");
            if(!mob.isAlive())killed++;
        }
        check(killed==spawned&&raid.getTotalRaidersAlive()==0,"real native kills remove first wave");
        check(((Set<?>)field(raid,"heroesOfTheVillage")).contains(b.body().getUUID()),"real raider death credits native hero UUID");
        // Remaining waves are not simulated here; exercise vanilla victory/reward
        // with the real hero from the first-wave kills, not a manually added hero.
        field(raid,"groupsSpawned",raid.getNumGroups(Difficulty.NORMAL)+1);
        for(int i=0;i<42;i++)raid.tick();
        check(raid.isVictory()&&b.hasEffect(MobEffects.HERO_OF_THE_VILLAGE),"vanilla victory grants hero to unique native effect owner");
        check(b.getEffect(MobEffects.HERO_OF_THE_VILLAGE).getDuration()==48000&&b.getEffect(MobEffects.HERO_OF_THE_VILLAGE).getAmplifier()==2,"original hero duration and level preserved");
        var reward=b.getEffect(MobEffects.HERO_OF_THE_VILLAGE);raid.tick();check(b.getEffect(MobEffects.HERO_OF_THE_VILLAGE)==reward,"victory reward not replaced every tick");
        if(account!=null)check(account.getStats().getValue(Stats.CUSTOM.get(Stats.RAID_WIN))==wins+1,"paired hero receives one real raid win stat");
        field(raid,"celebrationTicks",599);raid.tick();world.getRaids().tick();
        check(raid.isStopped()&&world.getRaids().get(raid.getId())==null,"vanilla celebration cleanup removes raid");
    }
    static void managedMap()throws Exception {
        freshRaid();
        var map=server.getLevel(ResourceKey.create(Registries.DIMENSION,ResourceLocation.fromNamespaceAndPath("goldcraft","raid_12345678")));
        check(map!=null,"real managed host dimension loaded from fixture datapack");
        var original=world;world=map;world.getChunk(0,0);village();
        var raid=new Raid(88,world,HOME);((Map<Integer,Raid>)field(world.getRaids(),"raidMap")).put(88,raid);
        var p=player(new UUID(0,916126));p.setRaidOmenPosition(HOME);
        HostWorldReset.activate(world,5000);
        check(raid.isStopped()&&world.getRaids().get(88)==null&&p.getRaidOmenPosition()==null,"actual map activation clears raids and paired pending origin");
        var current=new Raid(89,world,HOME);((Map<Integer,Raid>)field(world.getRaids(),"raidMap")).put(89,current);
        HostWorldReset.activate(world,5000);
        check(world.getRaids().get(89)==current&&!current.isStopped(),"same-epoch reconnect leaves current raid intact");
        HostWorldReset.clearRoundBlocks(world,5000,1);
        check(world.getRaids().get(89)==current&&!current.isStopped(),"block-only round clear does not reset raid manager");
        HostWorldReset.activate(world,5001);
        check(current.isStopped()&&world.getRaids().get(89)==null,"new map epoch clears current raid");
        cleanup();world=original;
    }
    static void run(ServerStartedEvent event){
        var report=new JsonObject();
        try{
            server=event.getServer();world=server.overworld();world.getChunk(0,0);bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture()){rules();lifecycle();incoming();multiple();noRaidsDimension();waveAndVictory(false);waveAndVictory(true);managedMap();}
            report.addProperty("passed",true);
        }catch(Throwable error){report.addProperty("passed",false);report.addProperty("error",error.toString());error.printStackTrace();}
        finally{
            report.addProperty("checks",checks);report.addProperty("realRaiders",spawned);report.addProperty("nativeKills",killed);
            try{fixtureReset();cleanup();Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-raids.json"),report.toString());}
            catch(Exception error){throw new RuntimeException(error);}
        }
    }
}
