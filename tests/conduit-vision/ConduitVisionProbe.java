package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.*;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.core.BlockPos;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.vehicle.Boat;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.block.Blocks;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.UUID;

/** Real effect/eye-fluid queries and publisher; host snapshots are a fixture. */
@Mod("goldcraft_conduit_vision_probe")
public final class ConduitVisionProbe {
    static int checks,packets;
    static long revision;
    static void check(boolean ok,String label){checks++;if(!ok)throw new AssertionError(label);}
    static void near(float actual,float expected,String label){check(Math.abs(actual-expected)<.00002f,label+": "+actual+" != "+expected);}
    public ConduitVisionProbe(){NeoForge.EVENT_BUS.addListener(ConduitVisionProbe::run);}
    static void view(HostWorldState.Actor a,boolean water){
        snapshot(a,100);
        var payload=new Wire.Writer().i64(host.epoch()).i64(host.tick()).i32(1)
            .i32(a.slot()).i32(a.serial()).i32(a.spawn()).i32(a.life())
            .f32(5).f32(6).f32(17).i32(water?1:0).toByteArray();
        check(host.nativeView().accept(host,payload,server.getTickCount()),"eye snapshot accepted");
    }
    static float sample(HostWorldState.Actor a,LivingEntity owner,boolean water)throws Exception {
        clock();view(a,water);return NativeUnderwaterVision.strength(a,owner);
    }
    static void packet(HealthBridgeFixture peer,HostWorldState.Actor a,float night)throws Exception {
        var bytes=peer.take(Wire.STATUS_VISION).payload();check(bytes.length==48,"vision packet length");
        var r=new Wire.Reader(bytes);check(r.i64()==host.epoch(),"vision epoch");
        var seq=r.i64();check(seq>revision,"vision revision");revision=seq;
        check(r.i32()==a.slot()&&r.i32()==a.serial()&&r.i32()==a.spawn()&&r.i32()==a.life(),"vision identity");
        near(r.f32(),night,"wire night/conduit coefficient");near(r.f32(),0,"blindness absent");
        near(r.f32(),0,"darkness absent");near(r.f32(),0,"darkness pulse absent");r.finish();packets++;
    }
    static void publish(HealthBridgeFixture peer,HostWorldState.Actor a,LivingEntity owner,float expected)throws Exception {
        NativeVision.update(a,owner);packet(peer,a,expected);
    }
    static void nativeForm(HealthBridgeFixture peer)throws Exception {
        fresh();revision=0;var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive(),"native real hitbox");packet(peer,a,0);
        box.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,10000));
        near(NativeUnderwaterVision.strength(a,box),0,"no eye evidence does not infer hitbox water");
        near(sample(a,box,false),0,"dry eye");
        near(sample(a,box,true),.006f,"first underwater tick");
        for(int n=0;n<10;n++)near(NativeUnderwaterVision.strength(a,box),.006f,"same tick does not accelerate adaptation");
        for(int n=2;n<=100;n++)sample(a,box,true);
        near(NativeUnderwaterVision.strength(a,box),.6f,"100 ticks");publish(peer,a,box,.6f);
        for(int n=101;n<=350;n++)sample(a,box,true);
        near(NativeUnderwaterVision.strength(a,box),.8f,"350 ticks");publish(peer,a,box,.8f);
        for(int n=351;n<=600;n++)sample(a,box,true);
        near(NativeUnderwaterVision.strength(a,box),1,"600 ticks");publish(peer,a,box,1);
        for(int n=0;n<20;n++)near(sample(a,box,true),1,"adaptation saturates");
        near(sample(a,box,false),0,"exit water brightness immediately zero");publish(peer,a,box,0);
        near(sample(a,box,true),.9928f,"exit decays ten and reentry advances one");
        box.removeEffect(MobEffects.CONDUIT_POWER);near(sample(a,box,true),0,"conduit removed immediately");publish(peer,a,box,0);
        box.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,10000));
        near(NativeUnderwaterVision.strength(a,box),.9936f,"adaptation retained while effect absent");
        box.addEffect(new MobEffectInstance(MobEffects.NIGHT_VISION,500));publish(peer,a,box,1);
        box.removeEffect(MobEffects.NIGHT_VISION);
        box.addEffect(new MobEffectInstance(MobEffects.NIGHT_VISION,199));
        publish(peer,a,box,.7f+(float)Math.sin(199*(float)Math.PI*.2f)*.3f);
        box.removeEffect(MobEffects.NIGHT_VISION);
        box.removeEffect(MobEffects.CONDUIT_POWER);
        box.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,1));step(box,1);
        check(!box.hasEffect(MobEffects.CONDUIT_POWER),"real effect timer expiry");publish(peer,a,box,0);
        box.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,10000));
        NativeUnderwaterVision.clear();
        for(int n=0;n<100;n++){box.removeEffect(MobEffects.CONDUIT_POWER);near(sample(a,box,true),0,"adapt before acquiring effect");}
        box.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,10000));
        near(NativeUnderwaterVision.strength(a,box),.6f,"effect acquired while already underwater");
        for(int n=0;n<7;n++)clock();near(NativeUnderwaterVision.strength(a,box),0,"expired view drops adaptation");
        near(sample(a,box,true),.006f,"fresh view after expiry restarts");
        snapshot(a,100);near(NativeUnderwaterVision.strength(a,box),0,"new actor snapshot needs matching eye evidence");
        view(a,true);near(NativeUnderwaterVision.strength(a,box),.006f,"snapshot recovery");
        a=actor(NONE,3,7,12,1,100,0);near(sample(a,box,true),.006f,"life reset");
        a=actor(NONE,3,8,12,1,100,0);near(sample(a,box,true),.006f,"spawn reset");
        a=actor(NONE,4,8,12,1,100,0);near(sample(a,box,true),.006f,"serial reset");
        peer.authority(false);near(NativeUnderwaterVision.strength(a,box),0,"native authority loss");
        peer.authority(true);near(sample(a,box,true),.006f,"native authority restored");
    }
    static void refreshEyes(Entity entity)throws Exception {
        var method=Entity.class.getDeclaredMethod("updateFluidOnEyes");method.setAccessible(true);method.invoke(entity);
    }
    static void minecraftForm(HealthBridgeFixture peer)throws Exception {
        fresh();revision=0;var p=player(new UUID(0,9123));var a=actor(p.getUUID(),4,9,13,113,100,0);
        snapshot(a,100);NativePlayers.tick(server,host);packet(peer,a,0);
        p.addEffect(new MobEffectInstance(MobEffects.CONDUIT_POWER,10000));
        p.setPos(8.5,100,8.5);var pos=new BlockPos(8,101,8);world.getChunk(0,0);
        for(int y=99;y<104;y++)world.setBlock(new BlockPos(8,y,8),Blocks.AIR.defaultBlockState(),3);
        world.setBlock(new BlockPos(8,100,8),Blocks.WATER.defaultBlockState(),3);refreshEyes(p);
        check(!p.isEyeInFluid(FluidTags.WATER),"shallow water below real player eyes");clock();near(NativeUnderwaterVision.strength(a,p),0,"MC shallow water no vision");
        world.setBlock(pos,Blocks.WATER.defaultBlockState(),3);refreshEyes(p);
        check(p.isEyeInFluid(FluidTags.WATER),"actual Entity eye fluid query");
        clock();near(NativeUnderwaterVision.strength(a,p),.006f,"MC first underwater tick");
        for(int n=2;n<=100;n++){clock();NativeUnderwaterVision.strength(a,p);}
        publish(peer,a,p,.6f);
        world.setBlock(pos,Blocks.LAVA.defaultBlockState(),3);refreshEyes(p);
        check(p.isEyeInFluid(FluidTags.LAVA)&&!p.isEyeInFluid(FluidTags.WATER),"lava FluidType differs");
        clock();near(NativeUnderwaterVision.strength(a,p),0,"lava is not conduit vision");
        world.setBlock(pos,Blocks.WATER.defaultBlockState(),3);refreshEyes(p);clock();
        near(NativeUnderwaterVision.strength(a,p),.546f,"lava exit decay followed by reentry");
        var boat=new Boat(world,8.5,101.4,8.5);world.addFreshEntity(boat);
        check(p.startRiding(boat,true),"actual boat mount");p.setPos(8.5,100,8.5);refreshEyes(p);
        check(!p.isEyeInFluid(FluidTags.WATER),"original boat interior eye-water exclusion");
        clock();near(NativeUnderwaterVision.strength(a,p),0,"boat interior excludes water vision");
        p.stopRiding();boat.discard();p.setPos(8.5,100,8.5);refreshEyes(p);
        NativeUnderwaterVision.clear();p.setGameMode(GameType.SPECTATOR);
        for(int n=1;n<=10;n++){clock();NativeUnderwaterVision.strength(a,p);}
        near(NativeUnderwaterVision.strength(a,p),.6f,"spectator adapts ten ticks per tick");
        p.setGameMode(GameType.SURVIVAL);
        // Form/life changes must not inherit the prior form's adaptation.
        a=actor(p.getUUID(),4,9,14,17,100,0);view(a,true);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box.hasEffect(MobEffects.CONDUIT_POWER)&&!p.hasEffect(MobEffects.CONDUIT_POWER),"unique native effect owner");
        packet(peer,a,.006f);
        a=actor(p.getUUID(),4,9,15,113,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        check(p.hasEffect(MobEffects.CONDUIT_POWER)&&box.isRemoved(),"MC effect owner restored");packet(peer,a,.006f);
    }
    static void run(ServerStartedEvent event){
        var result=new JsonObject();server=event.getServer();world=server.overworld();
        try{world.getChunk(0,0);bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(Wire.STATUS_VISION_CAP|Wire.NATIVE_VIEW_CAP)){
                check(GoldCraft.visionAvailable()&&GoldCraft.nativeViewAvailable(),"both negotiated");nativeForm(peer);minecraftForm(peer);
            }
            result.addProperty("passed",true);
        }catch(Throwable failure){result.addProperty("passed",false);result.addProperty("error",failure.toString());failure.printStackTrace();}
        finally{result.addProperty("checks",checks);result.addProperty("packets",packets);cleanup();
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("conduit-vision.json"),result.toString());}
            catch(Exception failure){throw new RuntimeException(failure);}}
    }
}
