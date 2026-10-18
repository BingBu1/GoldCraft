package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.world.*;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.behavior.GiveGiftToHero;
import net.minecraft.world.entity.ai.memory.MemoryModuleType;
import net.minecraft.world.entity.ai.sensing.PlayerSensor;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.npc.Villager;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.item.trading.ItemCost;
import net.minecraft.world.item.trading.MerchantOffer;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.TrialSpawnerBlock;
import net.minecraft.world.level.block.entity.TrialSpawnerBlockEntity;
import net.minecraft.world.level.block.entity.trialspawner.PlayerDetector;
import net.minecraft.world.level.block.entity.trialspawner.TrialSpawnerState;
import net.minecraft.world.level.block.entity.trialspawner.TrialSpawnerData;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceKey;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.world.phys.AABB;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.List;
import java.util.UUID;

/** Real vanilla consumers; host actors and controlled time/AI invocation are fixtures. */
@Mod("goldcraft_player_status_probe")
public final class PlayerStatusProbe {
    static int checks,gifts;
    static final BlockPos SPAWNER=new BlockPos(4,71,0);
    static void check(boolean ok,String label){checks++;if(!ok)throw new AssertionError(label);}
    public PlayerStatusProbe(){NeoForge.EVENT_BUS.addListener(PlayerStatusProbe::run);}
    static Object call(Class<?> type,Object target,String name,Class<?>[] types,Object... args)throws Exception {
        var m=type.getDeclaredMethod(name,types);m.setAccessible(true);return m.invoke(target,args);
    }
    static void sense(Villager villager)throws Exception {
        call(PlayerSensor.class,new PlayerSensor(),"doTick",new Class<?>[]{ServerLevel.class,LivingEntity.class},world,villager);
    }
    static List<UUID> detect(boolean sight){
        return PlayerDetector.NO_CREATIVE_PLAYERS.detect(world,PlayerDetector.EntitySelector.SELECT_FROM_LEVEL,SPAWNER,14,sight);
    }
    static void actors(List<HostWorldState.Actor> actors){
        var w=new Wire.Writer().i64(host.epoch()).i64(++tick).f32(0).i32(0).i32(actors.size());
        for(var a:actors){
            w.i32(a.slot()).i32(a.serial()).i32(a.userid()).i32(a.team()).i32(a.flags()).i32(a.life()).f32(a.health()).f32(a.armor());
            for(var v:List.of(a.origin(),a.velocity()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.f32(a.pitch()).f32(a.yaw());for(var v:List.of(a.mins(),a.maxs()))w.f32(v.x()).f32(v.y()).f32(v.z());
            w.bytes(Wire.uuid(a.minecraftPlayer())).i32(a.spawn()).i64(a.vitalsAck());
        }
        host.actors(w.toByteArray());NativePlayers.tick(server,host);
    }
    static void isolation()throws Exception {
        fresh();var first=actor(NONE,3,7,11,1,100,0);
        var second=new HostWorldState.Actor(3,8,43,1,1,11,100,0,new HostWorldState.Vector(64,0,228),first.velocity(),0,0,first.mins(),first.maxs(),NONE,7,0);
        actors(List.of(first,second));var a=NativePlayers.hitbox(first);var b=NativePlayers.hitbox(second);
        a.addEffect(new MobEffectInstance(MobEffects.BAD_OMEN,1000,1));b.addEffect(new MobEffectInstance(MobEffects.HERO_OF_THE_VILLAGE,1000,3));
        var ids=detect(false);check(ids.size()==2&&ids.stream().distinct().count()==2,"two ordinary native players have separate detector identities");
        check(!a.body().hasEffect(MobEffects.HERO_OF_THE_VILLAGE)&&!b.body().hasEffect(MobEffects.BAD_OMEN),"native players do not share effect state");
        check(PlayerDetector.EntitySelector.SELECT_FROM_LEVEL.getPlayers(world,p->p.hasEffect(MobEffects.HERO_OF_THE_VILLAGE)).equals(List.of(b.body())),"original selector predicate reads each unique effect owner");
        var origin=NativeStatusOrigin.capture(a.body());var oldId=a.body().getUUID();
        var changed=actor(NONE,3,7,12,1,100,0);actors(List.of(changed,second));a=NativePlayers.hitbox(changed);
        check(a.body().getUUID().equals(oldId)&&origin.resolve(world)==a.body(),"unpaired same-spawn form lifetime preserves gameplay identity");
        var reconnected=actor(NONE,4,7,13,1,100,0);actors(List.of(reconnected,second));
        check(origin.resolve(world)==null&&world.getPlayerByUUID(oldId)==null,"connection replacement cannot inherit prior UUID or status origin");
        check(b.body().hasEffect(MobEffects.HERO_OF_THE_VILLAGE),"other player's effects survive first connection replacement");
        var owner=NativePlayers.hitbox(reconnected);owner.addEffect(new MobEffectInstance(MobEffects.HERO_OF_THE_VILLAGE,100));
        var effect=owner.getEffect(MobEffects.HERO_OF_THE_VILLAGE);NativeStatusEffects.transfer(owner,owner.body());
        check(owner.body().getActiveEffectsMap().size()==1&&owner.getActiveEffectsMap().isEmpty()&&owner.body().getEffect(MobEffects.HERO_OF_THE_VILLAGE)==effect,"death handoff queries the logical player's own effect map");
    }
    static void trial()throws Exception {
        fresh();var actor=actor(NONE,3,7,11,1,100,0);snapshot(actor,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(actor);var body=box.body();
        var omen=new MobEffectInstance(MobEffects.BAD_OMEN,10000,2);box.addEffect(omen);
        check(body.hasEffect(MobEffects.BAD_OMEN)&&body.getEffect(MobEffects.BAD_OMEN)==box.getEffect(MobEffects.BAD_OMEN),"logical player queries unique effect instance");
        check(body.getActiveEffectsMap().isEmpty(),"logical body has no second timer map");
        check(world.players().isEmpty()&&server.getPlayerList().getPlayers().isEmpty(),"native discovery never registers a network player");
        check(world.getPlayerByUUID(body.getUUID())==body,"detector UUID resolves original logical body");
        check(detect(true).equals(List.of(body.getUUID())),"actual detector includes one native player with line of sight");
        check(PlayerDetector.NO_CREATIVE_PLAYERS.detect(world,PlayerDetector.EntitySelector.onlySelectPlayers(List.of()),SPAWNER,14,false).isEmpty(),"custom selector remains isolated");
        check(PlayerDetector.EntitySelector.SELECT_FROM_LEVEL.getPlayers(world,p->false).isEmpty(),"selector predicate respected");
        check(PlayerDetector.NO_CREATIVE_PLAYERS.detect(world,PlayerDetector.EntitySelector.SELECT_FROM_LEVEL,new BlockPos(80,71,0),14,false).isEmpty(),"detector range respected");
        for(int y=69;y<76;y++)for(int z=-3;z<=3;z++)world.setBlock(new BlockPos(2,y,z),Blocks.STONE.defaultBlockState(),3);
        check(detect(true).isEmpty()&&detect(false).size()==1,"vanilla line of sight remains authoritative");
        for(int y=69;y<76;y++)for(int z=-3;z<=3;z++)world.removeBlock(new BlockPos(2,y,z),false);
        world.setBlock(SPAWNER,Blocks.TRIAL_SPAWNER.defaultBlockState().setValue(TrialSpawnerBlock.STATE,TrialSpawnerState.WAITING_FOR_PLAYERS),3);
        var block=(TrialSpawnerBlockEntity)world.getBlockEntity(SPAWNER);block.setEntityId(EntityType.ZOMBIE,world.random);
        var spawner=block.getTrialSpawner();
        long scanTime=Math.floorMod(-SPAWNER.asLong(),20);
        server.getWorldData().overworldData().setGameTime(scanTime);
        spawner.tickServer(world,SPAWNER,false);
        check(spawner.isOminous()&&world.getBlockState(SPAWNER).getValue(TrialSpawnerBlock.OMINOUS),"original trial spawner becomes ominous");
        check(!box.hasEffect(MobEffects.BAD_OMEN)&&!body.hasEffect(MobEffects.BAD_OMEN),"bad omen removed from unique owner");
        check(box.getEffect(MobEffects.TRIAL_OMEN).getDuration()==54000,"original bad omen III converts to 45 minute trial omen");
        check(spawner.getState()==TrialSpawnerState.ACTIVE&&spawner.getData().countAdditionalPlayers(SPAWNER)==0,"one native participant activates actual trial");
        step(box,7);check(box.getEffect(MobEffects.TRIAL_OMEN).getDuration()==53993,"single original effect timer");
        spawner.getData().tryDetectPlayers(world,SPAWNER,spawner);
        check(box.getEffect(MobEffects.TRIAL_OMEN).getDuration()==53993&&spawner.getData().countAdditionalPlayers(SPAWNER)==0,"repeat scan neither renews omen nor duplicates participant");
        // Fast-forward the vanilla completion boundary, then use its actual
        // reward state and real loot table; do not manufacture item entities.
        var total=TrialSpawnerData.class.getDeclaredField("totalMobsSpawned");total.setAccessible(true);total.setInt(spawner.getData(),1000);
        spawner.tickServer(world,SPAWNER,true);
        check(spawner.getState()==TrialSpawnerState.WAITING_FOR_REWARD_EJECTION,"completed trial enters original reward phase");
        var loot=TrialSpawnerData.class.getDeclaredField("ejectingLootTable");loot.setAccessible(true);
        loot.set(spawner.getData(),java.util.Optional.of(ResourceKey.create(Registries.LOOT_TABLE,ResourceLocation.withDefaultNamespace("spawners/ominous/trial_chamber/key"))));
        server.getWorldData().overworldData().setGameTime(scanTime+40);spawner.tickServer(world,SPAWNER,true);
        check(spawner.getState()==TrialSpawnerState.EJECTING_REWARD,"original reward shutter opens");
        server.getWorldData().overworldData().setGameTime(scanTime+60);spawner.tickServer(world,SPAWNER,true);
        var rewards=world.getEntitiesOfClass(ItemEntity.class,new AABB(-10,60,-10,12,80,10));
        check(rewards.size()==1&&rewards.getFirst().getItem().is(Items.OMINOUS_TRIAL_KEY),"original ominous reward emits one real key for native participant");
        server.getWorldData().overworldData().setGameTime(scanTime+90);spawner.tickServer(world,SPAWNER,true);
        check(spawner.getState()==TrialSpawnerState.COOLDOWN&&world.getEntitiesOfClass(ItemEntity.class,new AABB(-10,60,-10,12,80,10)).size()==1,"participant consumed once before cooldown");
        for(var item:rewards)item.discard();
        box.removeEffect(MobEffects.TRIAL_OMEN);check(!body.hasEffect(MobEffects.TRIAL_OMEN),"effect removal immediately visible to logical queries");
        world.removeBlock(SPAWNER,false);
        snapshot(actor(NONE,3,7,11,0,0,0),100);NativePlayers.tick(server,host);
        check(detect(false).isEmpty()&&world.getPlayerByUUID(body.getUUID())==null,"death removes detector and UUID target");
    }
    static void village()throws Exception {
        fresh();var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);var body=box.body();
        var villager=EntityType.VILLAGER.create(world);villager.setPos(1,70,0);villager.setBaby(true);world.addFreshEntity(villager);
        sense(villager);
        check(villager.getBrain().getMemory(MemoryModuleType.NEAREST_VISIBLE_PLAYER).orElse(null)==body,"real villager sensor finds native player");
        check(villager.getBrain().getMemory(MemoryModuleType.NEAREST_PLAYERS).orElseThrow().size()==1,"sensor adds no duplicate player");
        var gift=new GiveGiftToHero(100);var delay=GiveGiftToHero.class.getDeclaredField("timeUntilNextGift");delay.setAccessible(true);delay.setInt(gift,0);
        check(!gift.tryStart(world,villager,0),"ordinary native player is not a hero");
        box.addEffect(new MobEffectInstance(MobEffects.HERO_OF_THE_VILLAGE,1000,2));
        var offer=new MerchantOffer(new ItemCost(Items.EMERALD,20),new ItemStack(Items.BREAD),10,1,0);
        villager.getOffers().clear();villager.getOffers().add(offer);
        call(Villager.class,villager,"updateSpecialPrices",new Class<?>[]{Player.class},body);
        check(offer.getCostA().getCount()==12,"actual hero III pricing subtracts floor(20 * .425)");
        check(gift.tryStart(world,villager,0),"actual hero gift task starts for native player");
        int before=world.getEntitiesOfClass(ItemEntity.class,new AABB(-10,60,-10,10,80,10)).size();
        for(int i=1;i<=22;i++)gift.tickOrStop(world,villager,i);
        var dropped=world.getEntitiesOfClass(ItemEntity.class,new AABB(-10,60,-10,10,80,10));
        check(dropped.size()==before+1&&dropped.stream().anyMatch(e->e.getItem().is(Items.POPPY)),"original baby villager throws one real poppy");gifts++;
        for(int i=23;i<50;i++)gift.tickOrStop(world,villager,i);
        check(world.getEntitiesOfClass(ItemEntity.class,new AABB(-10,60,-10,10,80,10)).size()==before+1,"completed gift does not repeat");
        box.removeEffect(MobEffects.HERO_OF_THE_VILLAGE);offer.resetSpecialPriceDiff();
        call(Villager.class,villager,"updateSpecialPrices",new Class<?>[]{Player.class},body);
        check(offer.getCostA().getCount()==20,"expired hero restores normal price");
        villager.setPos(40,70,0);sense(villager);
        check(villager.getBrain().getMemory(MemoryModuleType.NEAREST_VISIBLE_PLAYER).isEmpty(),"villager sensor range retained");
        villager.discard();for(var e:dropped)e.discard();
    }
    static void lifecycle()throws Exception {
        fresh();var p=player(new UUID(0,892367));p.setGameMode(GameType.SPECTATOR);
        var a=actor(p.getUUID(),3,7,11,17,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);var body=box.body();
        box.addEffect(new MobEffectInstance(MobEffects.TRIAL_OMEN,1000));
        check(detect(false).equals(List.of(body.getUUID())),"paired native form counted once, parked MC excluded");
        var effect=box.getEffect(MobEffects.TRIAL_OMEN);step(box,10);
        world.setBlock(SPAWNER,Blocks.TRIAL_SPAWNER.defaultBlockState().setValue(TrialSpawnerBlock.STATE,TrialSpawnerState.WAITING_FOR_PLAYERS),3);
        var trial=((TrialSpawnerBlockEntity)world.getBlockEntity(SPAWNER)).getTrialSpawner();
        server.getWorldData().overworldData().setGameTime(Math.floorMod(-SPAWNER.asLong(),20));
        trial.getData().tryDetectPlayers(world,SPAWNER,trial);
        check(trial.isOminous()&&trial.getData().countAdditionalPlayers(SPAWNER)==0,"direct trial omen activates ominous trial once");
        p.setGameMode(GameType.SURVIVAL);snapshot(actor(p.getUUID(),3,7,12,113,100,0),100);NativePlayers.tick(server,host);
        check(p.getEffect(MobEffects.TRIAL_OMEN)==effect&&effect.getDuration()==990,"form handoff retains exact instance and duration");
        check(detect(false).equals(List.of(p.getUUID())),"MC form detected once with real player identity");
        trial.getData().tryDetectPlayers(world,SPAWNER,trial);
        check(trial.getData().countAdditionalPlayers(SPAWNER)==0,"form switch does not inflate ongoing trial participants");
        p.setGameMode(GameType.CREATIVE);check(detect(false).isEmpty(),"real creative player excluded by original detector");
        p.setGameMode(GameType.SPECTATOR);snapshot(actor(p.getUUID(),3,7,13,17,100,0),100);NativePlayers.tick(server,host);
        var replacement=NativePlayers.hitbox(host.actors().getFirst());
        check(replacement.getEffect(MobEffects.TRIAL_OMEN)==effect&&p.getActiveEffectsMap().isEmpty(),"return to native keeps one effect owner");
        trial.getData().tryDetectPlayers(world,SPAWNER,trial);
        check(trial.getData().countAdditionalPlayers(SPAWNER)==0,"return to native still represents one trial participant");
        check(world.getEntity(p.getUUID())==replacement.body(),"gameplay UUID lookup resolves current native body");
        var stale=NativeStatusOrigin.capture(replacement.body());
        replacement.addEffect(new MobEffectInstance(MobEffects.HERO_OF_THE_VILLAGE,3));step(replacement,3);
        check(!replacement.body().hasEffect(MobEffects.HERO_OF_THE_VILLAGE),"original expiry clears queried hero effect");
        snapshot(actor(p.getUUID(),3,8,14,17,100,0),100);NativePlayers.tick(server,host);
        var respawn=NativePlayers.hitbox(host.actors().getFirst());
        check(!respawn.hasEffect(MobEffects.TRIAL_OMEN)&&world.getPlayerByUUID(replacement.body().getUUID())==respawn.body(),"new spawn keeps player UUID but never reuses the removed body or old effect");
        check(stale.resolve(world)==null,"old origin remains spawn-fenced despite stable player UUID");
        world.removeBlock(SPAWNER,false);
        var id=respawn.body().getUUID();fresh();check(detect(false).isEmpty()&&world.getPlayerByUUID(id)==null,"world reset removes player discovery");
    }
    static void run(ServerStartedEvent e){
        var report=new JsonObject();
        try{
            server=e.getServer();world=server.overworld();world.getChunk(0,0);
            bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture()){trial();village();lifecycle();isolation();}
            report.addProperty("passed",true);
        }catch(Throwable t){report.addProperty("passed",false);report.addProperty("error",t.toString());t.printStackTrace();}
        finally{
            report.addProperty("checks",checks);report.addProperty("realGifts",gifts);
            try{cleanup();Files.writeString(FMLPaths.GAMEDIR.get().resolve("player-status.json"),report.toString());}
            catch(Exception ex){throw new RuntimeException(ex);}
        }
    }
}
