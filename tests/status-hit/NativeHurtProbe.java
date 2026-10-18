package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.world.*;
import dev.goldcraft.bridge.Wire;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.effect.MobEffect;
import net.minecraft.world.effect.MobEffectCategory;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.monster.Silverfish;
import net.minecraft.world.phys.AABB;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import net.neoforged.bus.api.IEventBus;
import net.neoforged.neoforge.registries.DeferredRegister;
import net.minecraft.core.registries.Registries;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Mod("goldcraft_native_hurt_probe")
public final class NativeHurtProbe {
    static final AABB AREA=new AABB(-16,55,-16,16,90,16);
    static final DeferredRegister<MobEffect> EFFECTS=DeferredRegister.create(Registries.MOB_EFFECT,"goldcraft_native_hurt_probe");
    static final EffectObserver OBSERVER=new EffectObserver("observer");
    static final EffectObserver OTHER=new EffectObserver("other");
    // Both callbacks can replace the native incarnation; the second must stop.
    static final class EffectObserver {
        final net.neoforged.neoforge.registries.DeferredHolder<MobEffect,MobEffect> effect;
        EffectObserver(String name){effect=EFFECTS.register(name,()->new MobEffect(MobEffectCategory.NEUTRAL,0){
            @Override public void onMobHurt(LivingEntity owner,int amplifier,DamageSource source,float amount){
                observed.add(new Observation(owner,amplifier,source,amount));
                if(replace){replace=false;snapshot(actor(NONE,30,70,110,1,100,0),100);}
            }
        });}
    }
    record Observation(LivingEntity owner,int amplifier,DamageSource source,float amount){}
    static final List<Observation> observed=new ArrayList<>();
    static int checks,fishTotal;
    static long event;
    static boolean replace;
    public NativeHurtProbe(IEventBus bus){EFFECTS.register(bus);NeoForge.EVENT_BUS.addListener(NativeHurtProbe::run);}
    static void check(boolean ok,String message){checks++;if(!ok)throw new AssertionError(message);}
    static byte[] packet(long id,int kind,int serial,int spawn,int attacker){
        return new Wire.Writer().i64(host.epoch()).i64(id).i64(host.tick()+10)
            .i32(2).i32(serial).i32(spawn).i32(11).i32(attacker).i32(attacker==0?0:3).i32(attacker==0?0:7).i32(kind)
            .f32(20).f32(32).f32(64).f32(96).toByteArray();
    }
    static void receive(HealthBridgeFixture peer,byte[] payload)throws Exception {
        peer.send(Wire.NATIVE_HIT,payload);long deadline=System.nanoTime()+4_000_000_000L;
        while(true){
            for(var m:peer.link.drain())if(m.type()==Wire.NATIVE_HIT){NativeHurt.handle(server,m.payload());return;}
            if(System.nanoTime()>deadline)throw new AssertionError("TCP native notification timeout");Thread.sleep(2);
        }
    }
    static int fish(){return world.getEntitiesOfClass(Silverfish.class,AREA,e->!e.isRemoved()).size();}
    static void removeFish(){for(var e:world.getEntitiesOfClass(Silverfish.class,AREA,e->true))e.discard();}
    static void exercise(HealthBridgeFixture peer)throws Exception {
        fresh();NativeHurt.clear();var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null,"native effect owner exists");
        box.addEffect(new MobEffectInstance(MobEffects.ABSORPTION,10000,1));
        check(box.getAbsorptionAmount()==8,"original absorption application fills golden hearts");
        for(int i=0;i<3;i++)NativePlayers.tick(server,host);
        check(box.getAbsorptionAmount()==8,"empty logical proxy cannot clear its hitbox absorption during tick");
        box.removeEffect(MobEffects.ABSORPTION);
        box.addEffect(new MobEffectInstance(MobEffects.INFESTED,10000));
        for(int i=0;i<80;i++){
            box.getRandom().setSeed(i*1000003L+13);var bytes=packet(++event,2,3,7,0);receive(peer,bytes);
            int n=fish();fishTotal+=n;check(n<=2,"original silverfish count");
            receive(peer,bytes);check(fish()==n,"duplicate notification has no second RNG callback");removeFish();
        }
        check(fishTotal>0,"native notifications execute original infested effect");box.removeEffect(MobEffects.INFESTED);
        box.addEffect(new MobEffectInstance(OBSERVER.effect,10000,3));observed.clear();
        for(int kind=0;kind<=7;kind++){
            receive(peer,packet(++event,kind,3,7,2));var call=observed.getLast();
            check(call.owner()==box&&call.amplifier()==3&&call.amount()==4,"owner, effect level and native HP scaling");
            check(call.source().getEntity()==box.body(),"native attacker identity preserved");
            check(call.source().getWeaponItem()==null,"native damage cannot borrow an MC weapon's enchantments");
            check(call.source().getSourcePosition().equals(new net.minecraft.world.phys.Vec3(1,96/Wire.UNITS_PER_BLOCK+Wire.Y_OFFSET,-2)),"native impact coordinate mapping");
        }
        int count=observed.size();receive(peer,packet(++event,2,4,7,0));receive(peer,packet(++event,2,3,8,0));
        check(observed.size()==count,"replacement connection and respawn rejected");
        receive(peer,packet(event-3,2,3,7,0));check(observed.size()==count,"out-of-order notification rejected");
        var bytes=packet(++event,2,3,7,0);NativeHurt.clear();
        peer.authority(false);boolean rejected=false;try{NativeHurt.handle(server,bytes);}catch(IllegalArgumentException expected){rejected=true;}
        check(rejected&&observed.size()==count,"disconnected authority rejected");peer.authority(true);
        receive(peer,bytes);check(observed.size()==++count,"valid authority resumes");
        // Death actor state can arrive in this batch before effect-body cleanup.
        snapshot(actor(NONE,3,7,11,0,0,0),100);receive(peer,packet(++event,5,3,7,0));
        check(observed.size()==++count,"fatal settled hit still reaches its pre-cleanup effect owner");
        NativePlayers.tick(server,host);receive(peer,packet(++event,5,3,7,0));check(observed.size()==count,"retired body not recreated by a notification");

        fresh();observed.clear();var p=player(new UUID(0,72865));a=actor(p.getUUID(),3,7,11,17,100,0);
        snapshot(a,100);NativePlayers.tick(server,host);box=NativePlayers.hitbox(a);
        box.addEffect(new MobEffectInstance(OBSERVER.effect,10000,4));
        snapshot(actor(p.getUUID(),3,7,12,113,100,0),100);
        receive(peer,packet(++event,1,3,7,0));check(observed.size()==1&&observed.getLast().owner()==box,"pending form handoff uses existing effect owner");
        NativePlayers.tick(server,host);receive(peer,packet(++event,1,3,7,0));
        check(observed.size()==2&&observed.getLast().owner()==p,"completed form handoff uses real player");
        snapshot(actor(p.getUUID(),3,7,13,17,100,0),100);NativePlayers.tick(server,host);box=NativePlayers.hitbox(host.actors().getFirst());
        receive(peer,packet(++event,1,3,7,0));check(observed.size()==3&&observed.getLast().owner()==box,"reverse form handoff uses replacement native body");
        box.addEffect(new MobEffectInstance(OTHER.effect,10000));replace=true;receive(peer,packet(++event,1,3,7,0));
        check(observed.size()==4,"reentrant replacement stops remaining effect callbacks");
    }
    static void run(ServerStartedEvent e){
        var report=new JsonObject();
        try{
            server=e.getServer();world=server.overworld();world.getChunk(0,0);
            bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(Wire.NATIVE_HIT_CAP|Wire.STATUS_HEALTH_CAP|Wire.STATUS_EFFECTS)){exercise(peer);}
            report.addProperty("passed",true);
        }catch(Throwable t){report.addProperty("passed",false);report.addProperty("error",t.toString());t.printStackTrace();}
        finally{
            report.addProperty("checks",checks);report.addProperty("silverfish",fishTotal);
            try{cleanup();NativeHurt.clear();Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-hurt.json"),report.toString());}
            catch(Exception ex){throw new RuntimeException(ex);}
        }
    }
}
