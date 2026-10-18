package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.*;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.level.GameType;
import net.minecraft.network.protocol.game.ClientboundUpdateAttributesPacket;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.UUID;

/** Actual 1.21.1 LivingEntity/attribute/status-effect mixin regression. */
@Mod("goldcraft_native_health_probe")
public final class NativeHealthProbe {
    static int checks,healthMessages,damageMessages,vitalsMessages;
    static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    static void near(double value,double expected,String label){check(Math.abs(value-expected)<.001,label+" ["+value+" != "+expected+"]");}
    public NativeHealthProbe(){NeoForge.EVENT_BUS.addListener(NativeHealthProbe::run);}
    static void attributes(MinecraftServer server,ServerLevel world){
        var nativePlayer=EntityType.PIG.create(world);check(nativePlayer!=null,"native-owned test entity created");
        world.addFreshEntity(nativePlayer);
        var nativeAttribute=nativePlayer.getAttribute(Attributes.MAX_HEALTH);
        var ownership=(NativeHealthAttribute)(Object)nativeAttribute;
        double originalBase=nativeAttribute.getBaseValue();
        ownership.goldcraft$healthOwnership(true,false);
        nativeAttribute.setBaseValue(1200);
        near(nativePlayer.getMaxHealth(),1200,"authorized attribute extends above vanilla ceiling");
        nativePlayer.setHealth(1200);near(nativePlayer.getHealth(),1200,"native-owned health storage exceeds vanilla ceiling");
        near(nativeAttribute.save().getDouble("base"),originalBase,"temporary ownership never persists native class base");

        nativePlayer.addEffect(new MobEffectInstance(MobEffects.HEALTH_BOOST,100,0));
        near(nativePlayer.getMaxHealth(),1204,"real Health Boost modifier extends native-owned limit");
        nativePlayer.setHealth(1204);near(nativePlayer.getHealth(),1204,"modified native-owned HP stores above normal cap");
        nativePlayer.removeEffect(MobEffects.HEALTH_BOOST);
        near(nativePlayer.getMaxHealth(),1200,"effect expiry restores base class cap");
        near(nativePlayer.getHealth(),1204,"class cap change does not manufacture vanilla injury");

        var ordinary=EntityType.PIG.create(world);check(ordinary!=null,"ordinary control entity created");world.addFreshEntity(ordinary);
        var ordinaryAttribute=ordinary.getAttribute(Attributes.MAX_HEALTH);
        ordinaryAttribute.setBaseValue(1200);
        near(ordinary.getMaxHealth(),1024,"ordinary entity remains globally capped");
        ordinary.setHealth(1500);near(ordinary.getHealth(),1024,"ordinary health storage remains vanilla capped");
        check(!((NativeHealthAttribute)(Object)ordinaryAttribute).goldcraft$healthOwned(),"ownership cannot bleed to other entity instances");

        nativePlayer.removeEffect(MobEffects.HEALTH_BOOST);
        ownership.goldcraft$healthOwnership(false,true);
        near(nativeAttribute.getBaseValue(),originalBase,"detached entity restores its pre-ownership attribute base");
    }
    static void health(HealthBridgeFixture peer,float base,float effective)throws Exception {
        var a=host.actors().getFirst();var r=new Wire.Reader(peer.take(Wire.STATUS_HEALTH).payload());
        check(r.i64()==host.epoch()&&r.i64()>0,"production health publisher carries world and revision");
        check(r.i32()==a.slot()&&r.i32()==a.serial()&&r.i32()==a.spawn()&&r.i32()==a.life(),"health publisher carries current identity and life");
        near(r.f32(),base,"published class base");near(r.f32(),effective,"published effective cap");r.finish();healthMessages++;
    }
    static long damage(HealthBridgeFixture peer,float amount)throws Exception {
        var r=new Wire.Reader(peer.take(Wire.DAMAGE_REQUEST).payload());var a=host.actors().getFirst();
        check(r.i64()==host.epoch(),"effect damage world");long event=r.i64();r.i64();
        check(r.i32()==a.slot()&&r.i32()==a.serial()&&r.i32()==a.spawn()&&r.i32()==5,"actual effect health request identity");
        near(r.f32(),amount,"actual effect health request amount");damageMessages++;return event;
    }
    static void acknowledge(long event,float nativeHealth){
        var a=host.actors().getFirst();NativePlayers.result(new Wire.Writer().i64(host.epoch()).i64(event)
            .i32(a.slot()).i32(a.serial()).i32(a.spawn()).i32(0).f32(nativeHealth).f32(0).i64(host.tick()).toByteArray());
    }
    static long vitals(HealthBridgeFixture peer,float delta)throws Exception {
        var a=host.actors().getFirst();var r=new Wire.Reader(peer.take(Wire.VITALS_DELTA).payload());
        check(r.i64()==host.epoch(),"paired vitals world");long event=r.i64();
        check(r.i32()==a.slot()&&r.i32()==a.serial()&&r.i32()==a.spawn(),"paired vitals identity");
        r.bytes(16);near(r.f32(),delta,"paired vitals actual health delta");vitalsMessages++;return event;
    }
    static void nativeEffects(HealthBridgeFixture peer)throws Exception {
        fresh();var a=actor(NONE,3,7,11,1,6000,0);snapshot(a,6000);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive(),"production native body exists");
        near(box.getMaxHealth(),1200,"native hitbox uses class base above vanilla limit");
        near(box.body().getMaxHealth(),1200,"logical native Player reads same cap");health(peer,6000,6000);
        box.addEffect(new MobEffectInstance(MobEffects.HEALTH_BOOST,3,4));
        near(box.getMaxHealth(),1220,"actual proxy Health Boost affects effective cap");
        near(box.body().getMaxHealth(),1220,"logical Player exposes proxy modifier");
        box.addEffect(new MobEffectInstance(MobEffects.HEAL,1,0));step(box,1);
        long event=damage(peer,-20);near(box.getHealth(),1204,"native heal prediction uses new cap");
        near(box.body().getHealth(),1200,"unacknowledged heal leaves host HP authoritative");
        acknowledge(event,6020);check(box.body().healthLedger().pending()==0,"production native acknowledgement settles healing");
        snapshot(actor(NONE,3,7,11,1,6020,0),6000);NativePlayers.tick(server,host);health(peer,6000,6100);
        step(box,2);check(!box.hasEffect(MobEffects.HEALTH_BOOST),"real proxy tick expires Health Boost");
        near(box.getMaxHealth(),1200,"expired modifier restores class cap");
        near(box.getHealth(),1204,"expiry awaits native cap reconciliation without a second injury");
        check(box.body().healthLedger().pending()==0,"expiry creates no native damage request");
        NativePlayers.tick(server,host);health(peer,6000,6000);
        snapshot(actor(NONE,3,7,11,1,6000,0),6000);NativePlayers.tick(server,host);health(peer,6000,6000);
        near(box.getHealth(),1200,"next authoritative snapshot reconciles cap reduction");
        snapshot(actor(NONE,3,7,11,1,7500,0),6000);NativePlayers.tick(server,host);health(peer,6000,6000);
        near(box.getHealth(),1500,"host reward above cap survives snapshot");
        box.addEffect(new MobEffectInstance(MobEffects.HARM,1,0));step(box,1);event=damage(peer,30);
        near(box.getHealth(),1494,"harm subtracts from reward HP");
        box.addEffect(new MobEffectInstance(MobEffects.HEAL,1,0));step(box,1);
        near(box.getHealth(),1494,"healing cannot refill above class cap");
        check(box.body().healthLedger().pending()==1,"over-cap healing adds no native request");
        acknowledge(event,7470);snapshot(actor(NONE,3,7,11,1,7470,0),6000);NativePlayers.tick(server,host);health(peer,6000,6000);
        near(box.getHealth(),1494,"reward damage receipt remains authoritative");
        box.addEffect(new MobEffectInstance(MobEffects.HEALTH_BOOST,200,1));
        snapshot(actor(NONE,3,7,12,4,1,0),6000);NativePlayers.tick(server,host);
        check(box.isRemoved()&&!NativeHealth.owned(box),"native death retires effect owner and range override");
        snapshot(actor(NONE,3,8,13,1,6000,0),6000);NativePlayers.tick(server,host);health(peer,6000,6000);
        var reborn=NativePlayers.hitbox(host.actors().getFirst());
        check(reborn!=box&&reborn.getActiveEffects().isEmpty(),"native respawn has a fresh effect lifetime");
        snapshot(null,6000);NativePlayers.tick(server,host);
        check(reborn.isRemoved()&&!NativeHealth.owned(reborn),"empty actor snapshot prunes ownership");
    }
    static void pairedVitals(HealthBridgeFixture peer)throws Exception {
        fresh();var p=player(new UUID(0,9041));var a=actor(p.getUUID(),3,7,11,113,100,0);snapshot(a,100);
        SharedVitals.prepare(p,host,a);SharedVitals.active(p,true);NativePlayers.tick(server,host);health(peer,100,100);
        p.addEffect(new MobEffectInstance(MobEffects.HEALTH_BOOST,200,1));p.heal(4);
        near(p.getHealth(),24,"actual ServerPlayer heals into increased cap");SharedVitals.capture(server,host);
        long ack=vitals(peer,20);check(SharedVitals.pending(p.getUUID())==1,"paired healing retained until acknowledgement");
        SharedVitals.prepare(p,host,a);near(p.getHealth(),24,"old host snapshot does not replay healing");
        NativePlayers.tick(server,host);health(peer,100,140);
        p.removeEffect(MobEffects.HEALTH_BOOST);near(p.getMaxHealth(),20,"paired effect removal restores cap");
        near(p.getHealth(),24,"paired removal does not invent damage");SharedVitals.capture(server,host);
        check(SharedVitals.pending(p.getUUID())==1,"effect removal adds no VITALS_DELTA");
        snapshot(actor(p.getUUID(),3,7,11,113,100,ack),100);SharedVitals.prepare(p,host,host.actors().getFirst());
        near(p.getHealth(),20,"authoritative expiry snapshot reconciles paired HP");check(SharedVitals.pending(p.getUUID())==0,"paired native ack drains healing");
        snapshot(actor(p.getUUID(),3,7,11,113,50,ack),100);SharedVitals.prepare(p,host,host.actors().getFirst());
        p.invulnerableTime=0;check(p.hurt(world.damageSources().generic(),2),"actual ServerPlayer hurt pipeline executes");
        SharedVitals.capture(server,host);vitals(peer,-10);p.heal(1);SharedVitals.capture(server,host);vitals(peer,5);
        snapshot(actor(p.getUUID(),3,7,11,113,175,ack),100);SharedVitals.prepare(p,host,host.actors().getFirst());
        near(p.getHealth(),33,"pending damage plus heal cannot refill newly received reward above cap");
        check(SharedVitals.pending(p.getUUID())==2,"new reward snapshot does not acknowledge old deltas");
        SharedVitals.disconnect(p.getUUID());near(p.getAttribute(Attributes.MAX_HEALTH).getBaseValue(),20,"disconnect restores saved player attribute");
        check(!NativeHealth.owned(p),"disconnect releases player attribute ownership");
    }
    static void formAndDeath(HealthBridgeFixture peer)throws Exception {
        fresh();var p=player(new UUID(0,9042));
        p.getAttribute(Attributes.MAX_HEALTH).setBaseValue(37);
        snapshot(actor(p.getUUID(),3,7,11,113,6000,0),6000);
        SharedVitals.prepare(p,host,host.actors().getFirst());SharedVitals.active(p,true);
        var boost=new MobEffectInstance(MobEffects.HEALTH_BOOST,200,1);p.addEffect(boost);
        NativePlayers.tick(server,host);health(peer,6000,6040);near(p.getMaxHealth(),1208,"paired high-HP modifier is not clamped at 1024");
        snapshot(actor(p.getUUID(),3,7,12,17,6000,0),6000);HostSession.hold(p);SharedVitals.active(p,false);
        NativePlayers.tick(server,host);health(peer,6000,6040);var box=NativePlayers.hitbox(host.actors().getFirst());
        check(box.getEffect(MobEffects.HEALTH_BOOST)==boost&&p.getActiveEffects().isEmpty(),"MC to CS moves one effect instance");
        near(p.getMaxHealth(),1200,"HUD mirroring does not add server-side duplicate modifier");
        check(clientPackets.stream().anyMatch(x->x instanceof ClientboundUpdateAttributesPacket),"real attribute packet generated for paired native HUD");
        step(box,1);check(boost.getDuration()==199,"native form ticks effect once");
        snapshot(actor(p.getUUID(),3,7,13,113,6000,0),6000);p.setGameMode(GameType.SURVIVAL);
        NativePlayers.tick(server,host);health(peer,6000,6040);SharedVitals.prepare(p,host,host.actors().getFirst());SharedVitals.active(p,true);
        check(p.getEffect(MobEffects.HEALTH_BOOST)==boost&&boost.getDuration()==199&&box.isRemoved(),"CS to MC retains exact effect and duration");
        check(!NativeHealth.owned(box),"removed native proxy releases range override");
        snapshot(actor(p.getUUID(),3,7,14,20,1,0),6000);var dead=host.actors().getFirst();SharedVitals.prepare(p,host,dead);
        check(!p.isAlive(),"real paired death confirmed by host flags, not numeric health");
        var follow=Class.forName("dev.goldcraft.world.HostDeathView").getDeclaredMethod("follow",MinecraftServer.class,ServerLevel.class,HostWorldState.class,HostWorldState.Actor.class,ServerPlayer.class);
        follow.setAccessible(true);var viewer=(ServerPlayer)follow.invoke(null,server,world,host,dead,p);
        check(viewer!=p&&viewer.isAlive()&&viewer.isSpectator()&&p.isRemoved(),"actual PlayerList respawn provides death viewer");
        NativePlayers.tick(server,host);near(viewer.getMaxHealth(),37,"spectator replacement does not inherit temporary class base");
        check(viewer.getActiveEffects().isEmpty()&&!NativeHealth.owned(p),"death clears effects and old body ownership");
        snapshot(actor(viewer.getUUID(),3,8,15,113,10000,0),10000);viewer.setGameMode(GameType.SURVIVAL);
        SharedVitals.prepare(viewer,host,host.actors().getFirst());NativePlayers.tick(server,host);health(peer,10000,10000);
        near(viewer.getMaxHealth(),2000,"new spawn applies new class base");near(viewer.getHealth(),2000,"new spawn restores authoritative health");
        peer.authority(false);check(!NativeHealth.prepare(viewer,host.actors().getFirst()),"capability withdrawal releases ownership");
        near(viewer.getAttribute(Attributes.MAX_HEALTH).getBaseValue(),37,"capability withdrawal restores original custom base");peer.authority(true);
        NativeHealth.prepare(viewer,host.actors().getFirst());
        host.world(new Wire.Writer().i64(++epoch).string("cs_assault").toByteArray());NativeHealth.prune(host);
        check(!NativeHealth.owned(viewer),"map epoch change prunes player ownership");near(viewer.getMaxHealth(),37,"map epoch reset restores original max health");
    }
    static void respawnCopies()throws Exception {
        for(boolean alive:new boolean[]{false,true}){
            fresh();var p=player(new UUID(0,alive?9051:9050));
            p.getAttribute(Attributes.MAX_HEALTH).setBaseValue(43);
            snapshot(actor(p.getUUID(),3,7,11,113,6000,0),6000);
            SharedVitals.prepare(p,host,host.actors().getFirst());
            p.addEffect(new MobEffectInstance(MobEffects.HEALTH_BOOST,200,1));
            var clone=new double[]{-1};
            java.util.function.Consumer<net.neoforged.neoforge.event.entity.player.PlayerEvent.Clone> observer=e->{
                if(e.getOriginal()==p)clone[0]=e.getEntity().getAttribute(Attributes.MAX_HEALTH).getBaseValue();
            };
            NeoForge.EVENT_BUS.addListener(observer);
            ServerPlayer replacement;
            try{replacement=server.getPlayerList().respawn(p,alive,alive
                ?net.minecraft.world.entity.Entity.RemovalReason.CHANGED_DIMENSION
                :net.minecraft.world.entity.Entity.RemovalReason.KILLED);}
            finally{NeoForge.EVENT_BUS.unregister(observer);}
            replacement.connection.player=replacement;
            near(clone[0],43,"Clone listeners see original custom base, alive="+alive);
            near(replacement.getMaxHealth(),alive?51:43,"actual respawn retains vanilla effect-copy semantics, alive="+alive);
            check(!NativeHealth.owned(replacement),"replacement awaits its own authenticated binding");
            SharedVitals.prepare(replacement,host,host.actors().getFirst());
            near(replacement.getMaxHealth(),alive?1208:1200,"rebound replacement receives native class limit");
            NativeHealth.detach(replacement);near(replacement.getAttribute(Attributes.MAX_HEALTH).getBaseValue(),43,"new binding preserves original base");
        }
        fresh();var ordinary=player(new UUID(0,9052));ordinary.getAttribute(Attributes.MAX_HEALTH).setBaseValue(91);
        var replacement=server.getPlayerList().respawn(ordinary,false,net.minecraft.world.entity.Entity.RemovalReason.KILLED);
        replacement.connection.player=replacement;
        near(replacement.getMaxHealth(),91,"ordinary unpaired respawn keeps vanilla custom base");
    }
    static void run(ServerStartedEvent event){
        var result=new JsonObject();server=event.getServer();world=server.overworld();
        try{world.getChunk(0,0);bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            attributes(server,world);
            try(var peer=new HealthBridgeFixture()){
                check(GoldCraft.healthLimitsAvailable(),"real negotiated health capability available");
                nativeEffects(peer);pairedVitals(peer);formAndDeath(peer);respawnCopies();NativePlayerAttributeChecks.run();
            }
            result.addProperty("passed",true);
        }
        catch(Throwable failure){result.addProperty("passed",false);result.addProperty("error",failure.toString());failure.printStackTrace();}
        finally{
            result.addProperty("checks",checks);
            result.addProperty("healthMessages",healthMessages);result.addProperty("damageMessages",damageMessages);result.addProperty("vitalsMessages",vitalsMessages);
            cleanup();
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-health.json"),result.toString());}
            catch(Exception failure){throw new RuntimeException(failure);}
        }
    }
}
