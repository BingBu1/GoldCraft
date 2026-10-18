package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.world.*;
import dev.goldcraft.bridge.Wire;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.effect.MobEffect;
import net.minecraft.world.effect.MobEffectCategory;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.monster.Silverfish;
import net.minecraft.world.entity.monster.Zombie;
import net.minecraft.world.phys.AABB;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.living.LivingIncomingDamageEvent;
import net.neoforged.neoforge.event.entity.living.LivingDamageEvent;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import net.neoforged.bus.api.IEventBus;
import net.neoforged.neoforge.registries.DeferredRegister;
import net.minecraft.core.registries.Registries;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/** Executes real hurt/effect callbacks. Native actor input and receipts are fixtures. */
@Mod("goldcraft_status_hit_probe")
public final class StatusHitProbe {
    static final AABB AREA=new AABB(-16,55,-16,16,90,16);
    static int checks,requests,fishBeforeReceipt,fishAfterAccepted;
    static boolean cancel,nested,extraDelta;
    static final DeferredRegister<MobEffect> EFFECTS=DeferredRegister.create(Registries.MOB_EFFECT,"goldcraft_status_hit_probe");
    static final net.neoforged.neoforge.registries.DeferredHolder<MobEffect,MobEffect> OBSERVER=EFFECTS.register("observer",()->new MobEffect(MobEffectCategory.NEUTRAL,0){
        @Override public void onMobHurt(LivingEntity owner,int amplifier,DamageSource source,float amount){
            observed.add(new Observation(owner,amplifier,source,amount));
            if(nested){nested=false;owner.invulnerableTime=0;owner.hurt(source,.5f);}
        }
    });
    record Observation(LivingEntity owner,int amplifier,DamageSource source,float amount){}
    static final List<Observation> observed=new ArrayList<>();
    static void check(boolean ok,String label){checks++;if(!ok)throw new AssertionError(label);}
    public StatusHitProbe(IEventBus bus){
        EFFECTS.register(bus);
        NeoForge.EVENT_BUS.addListener(StatusHitProbe::run);
        NeoForge.EVENT_BUS.addListener((LivingIncomingDamageEvent e)->{
            if(cancel&&e.getEntity() instanceof NativePlayerHitboxEntity)e.setCanceled(true);
        });
        NeoForge.EVENT_BUS.addListener((LivingDamageEvent.Post e)->{
            if(extraDelta&&e.getEntity() instanceof NativePlayerHitboxEntity){
                extraDelta=false;e.getEntity().setHealth(e.getEntity().getHealth()-.25f);
            }
        });
    }
    static int fish(){return world.getEntitiesOfClass(Silverfish.class,AREA,e->!e.isRemoved()).size();}
    static void removeFish(){for(var e:world.getEntitiesOfClass(Silverfish.class,AREA,e->true))e.discard();}
    static void receipt(byte[] payload,int status)throws Exception {receipt(payload,status,100,0);}
    static void receipt(byte[] payload,int status,float health,float armor)throws Exception {
        var r=new Wire.Reader(payload);long epoch=r.i64(),event=r.i64();r.i64();
        int slot=r.i32(),serial=r.i32(),spawn=r.i32(),kind=r.i32();
        var response=new Wire.Writer().i64(epoch).i64(event).i32(slot).i32(serial).i32(spawn).i32(status).f32(0).f32(0);
        if(kind==5)response.i64(host.tick());var bytes=response.toByteArray();
        var buffer=java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.LITTLE_ENDIAN);buffer.putFloat(32,health);buffer.putFloat(36,armor);
        if(!Float.isFinite(health)||!Float.isFinite(armor)){
            boolean rejected=false;try{NativePlayers.result(bytes);}catch(IllegalArgumentException expected){rejected=true;}
            check(rejected,"nonfinite authority receipt rejected by decoder");
        }else NativePlayers.result(bytes);
    }
    static void exercise(HealthBridgeFixture peer)throws Exception {
        fresh();var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive(),"native proxy alive");
        var mob=EntityType.ZOMBIE.create(world);mob.setPos(1,70,0);world.addFreshEntity(mob);
        MinecraftObjects.tick(server,host);peer.take(Wire.MINECRAFT_OBJECTS);
        check(MinecraftObjects.key(mob)!=0,"real mob registered for native authority");
        box.addEffect(new MobEffectInstance(MobEffects.INFESTED,10000));
        var source=world.damageSources().mobAttack(mob);
        // Deterministic original RNG, real LivingEntity.hurt and real silverfish.
        for(int i=0;i<80;i++){
            box.invulnerableTime=0;box.getRandom().setSeed(i+13);
            check(box.hurt(source,1),"hurt request accepted by transport");
            var packet=peer.take(Wire.DAMAGE_REQUEST);requests++;
            fishBeforeReceipt+=fish();removeFish();
            receipt(packet.payload(),4);check(fish()==0,"rejected receipt does not create fish");
        }
        check(fishBeforeReceipt==0,"infested must wait for native hit confirmation, observed "+fishBeforeReceipt);
        for(int i=0;i<80;i++){
            box.invulnerableTime=0;box.getRandom().setSeed(i+13);
            check(box.hurt(source,1),"accepted hit request");var packet=peer.take(Wire.DAMAGE_REQUEST);requests++;
            check(fish()==0,"still no fish before successful receipt");receipt(packet.payload(),0);
            int count=fish();fishAfterAccepted+=count;check(count<=2,"vanilla silverfish count bound");
            receipt(packet.payload(),0);check(fish()==count,"duplicate receipt cannot replay callback");removeFish();
        }
        check(fishAfterAccepted>0,"confirmed real hurts trigger original infested spawning");
        cancel=true;
        for(int i=0;i<80;i++){box.invulnerableTime=0;check(!box.hurt(source,1),"NeoForge cancellation preserved");}
        cancel=false;check(fish()==0,"cancelled hurt does not spawn fish");
        mob.discard();
    }
    static void callbacks(HealthBridgeFixture peer)throws Exception {
        fresh();observed.clear();var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);var mob=EntityType.ZOMBIE.create(world);mob.setPos(1,70,0);world.addFreshEntity(mob);
        MinecraftObjects.tick(server,host);peer.take(Wire.MINECRAFT_OBJECTS);
        box.addEffect(new MobEffectInstance(OBSERVER,10000,2));var source=world.damageSources().mobAttack(mob);
        check(box.hurt(source,2),"observer hit requested");var packet=peer.take(Wire.DAMAGE_REQUEST);
        check(observed.isEmpty(),"custom effect waits for receipt");
        receipt(packet.payload(),0,Float.NaN,0);receipt(packet.payload(),0,100,Float.POSITIVE_INFINITY);
        check(observed.isEmpty(),"nonfinite receipts do not dispatch or consume pending callback");
        box.addEffect(new MobEffectInstance(OBSERVER,10000,6));
        receipt(packet.payload(),0);check(observed.size()==1,"one custom effect callback");
        var call=observed.getFirst();check(call.owner()==box&&call.amplifier()==2,"impact owner and amplifier retained");
        check(call.source()==source&&call.source().getEntity()==mob&&call.amount()==2,"exact source and post-MC amount retained");
        receipt(packet.payload(),0);check(observed.size()==1,"duplicate custom callback refused");
        nested=true;box.invulnerableTime=0;check(box.hurt(source,1),"nested parent hit");packet=peer.take(Wire.DAMAGE_REQUEST);
        receipt(packet.payload(),0);check(observed.size()==2,"parent callback only before nested confirmation");
        var child=peer.take(Wire.DAMAGE_REQUEST);receipt(child.payload(),0);
        check(observed.size()==3&&observed.getLast().amount()==.5f,"nested callback owns a separate receipt");
        receipt(child.payload(),0);check(observed.size()==3,"nested duplicate suppressed");
        box.invulnerableTime=0;check(box.hurt(source,1),"expiry hit requested");packet=peer.take(Wire.DAMAGE_REQUEST);
        box.removeEffect(OBSERVER);receipt(packet.payload(),0);
        check(observed.size()==4&&observed.getLast().amplifier()==6,"effect removal does not erase an accepted earlier hit");
        box.addEffect(new MobEffectInstance(OBSERVER,10000));
        peer.authority(false);box.invulnerableTime=0;check(!box.hurt(source,1),"unavailable transport refuses native hit");
        check(observed.size()==4,"failed send has no callback");peer.authority(true);
        box.invulnerableTime=0;check(box.hurt(source,1),"lethal outcome request");packet=peer.take(Wire.DAMAGE_REQUEST);
        receipt(packet.payload(),0,0,0);check(observed.size()==5,"lethal confirmed outcome still invokes hurt callback");
        snapshot(a,100);snapshot(a,100);NativePlayers.tick(server,host);
        box.addEffect(new MobEffectInstance(MobEffects.ABSORPTION,1000,0));
        box.invulnerableTime=0;float hearts=box.getAbsorptionAmount();check(box.hurt(source,1),"fully absorbed original MC hit accepted");
        check(box.getAbsorptionAmount()<hearts&&observed.size()==6,"MC absorption-only hit preserves one local callback");
        box.removeEffect(MobEffects.ABSORPTION);
        box.invulnerableTime=0;check(box.hurt(source,1),"old spawn request");packet=peer.take(Wire.DAMAGE_REQUEST);
        snapshot(actor(NONE,3,8,12,1,100,0),100);NativePlayers.tick(server,host);receipt(packet.payload(),0);
        check(observed.size()==6,"old spawn receipt has no new-life callback");
        mob.discard();

        fresh();observed.clear();var p=player(new UUID(0,72864));a=actor(p.getUUID(),3,7,11,17,100,0);
        snapshot(a,100);NativePlayers.tick(server,host);box=NativePlayers.hitbox(a);
        mob=EntityType.ZOMBIE.create(world);mob.setPos(1,70,0);world.addFreshEntity(mob);
        MinecraftObjects.tick(server,host);peer.take(Wire.MINECRAFT_OBJECTS);
        box.addEffect(new MobEffectInstance(OBSERVER,10000,4));source=world.damageSources().mobAttack(mob);
        check(box.hurt(source,1),"form handoff pending hit");packet=peer.take(Wire.DAMAGE_REQUEST);
        snapshot(actor(p.getUUID(),3,7,12,113,100,0),100);NativePlayers.tick(server,host);
        check(box.isRemoved()&&p.hasEffect(OBSERVER),"real effect owner transferred to MC player");
        receipt(packet.payload(),0);check(observed.size()==1&&observed.getFirst().owner()==p,"same spawn callback follows current form owner");
        // Ordinary real MC damage remains the vanilla synchronous callback.
        p.invulnerableTime=0;check(p.hurt(source,1),"ordinary real player hurt");
        check(observed.size()==2&&observed.getLast().owner()==p,"real player callback is not delayed or duplicated");
        snapshot(actor(p.getUUID(),3,7,13,17,100,0),100);NativePlayers.tick(server,host);box=NativePlayers.hitbox(host.actors().getFirst());
        box.invulnerableTime=0;check(box.hurt(source,1),"reverse form pending hit");packet=peer.take(Wire.DAMAGE_REQUEST);
        snapshot(actor(p.getUUID(),3,7,14,113,100,0),100);NativePlayers.tick(server,host);
        snapshot(actor(p.getUUID(),3,7,15,17,100,0),100);NativePlayers.tick(server,host);
        var replacement=NativePlayers.hitbox(host.actors().getFirst());
        check(replacement!=box&&box.isRemoved(),"form return replaces the old hitbox");
        receipt(packet.payload(),0);check(observed.size()==3&&observed.getLast().owner()==replacement,"same spawn callback follows replacement native owner");
        replacement.invulnerableTime=0;check(replacement.hurt(source,1),"old authority pending hit");packet=peer.take(Wire.DAMAGE_REQUEST);
        var generation=dev.goldcraft.GoldCraft.class.getDeclaredField("generation");generation.setAccessible(true);long previous=generation.getLong(null);
        generation.setLong(null,previous+1);receipt(packet.payload(),0);check(observed.size()==3,"wrong authority cannot execute pending callback");
        generation.setLong(null,previous);receipt(packet.payload(),4);check(observed.size()==3,"rejected callback removed after authority restored");
        extraDelta=true;replacement.invulnerableTime=0;check(replacement.hurt(source,1),"Mod emits two native deltas in one hurt");
        var first=peer.take(Wire.DAMAGE_REQUEST);var second=peer.take(Wire.DAMAGE_REQUEST);
        receipt(first.payload(),4);check(observed.size()==3,"first rejected delta does not dispatch callback");
        receipt(second.payload(),0);check(observed.size()==4,"later accepted delta confirms original hit once");
        receipt(first.payload(),0);receipt(second.payload(),0);check(observed.size()==4,"mixed receipt duplicates cannot replay callback");
        mob.discard();
    }
    static void run(ServerStartedEvent e){
        var report=new JsonObject();
        try{
            server=e.getServer();world=server.overworld();world.getChunk(0,0);
            bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture()){exercise(peer);callbacks(peer);}
            report.addProperty("passed",true);
        }catch(Throwable t){report.addProperty("passed",false);report.addProperty("error",t.toString());t.printStackTrace();}
        finally{
            cancel=false;report.addProperty("checks",checks);report.addProperty("requests",requests);
            report.addProperty("fishBeforeReceipt",fishBeforeReceipt);report.addProperty("fishAfterAccepted",fishAfterAccepted);
            try{cleanup();MinecraftObjects.clear();Files.writeString(FMLPaths.GAMEDIR.get().resolve("status-hit.json"),report.toString());}
            catch(Exception ex){throw new RuntimeException(ex);}
        }
    }
}
