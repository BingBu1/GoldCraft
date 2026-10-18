package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.*;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.UUID;

/** Actual effect instances, removal Mixins and transport from the production JAR. */
@Mod("goldcraft_native_nausea_probe")
public final class NativeNauseaProbe {
    static int checks,packets;static long revision;
    static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    public NativeNauseaProbe(){NeoForge.EVENT_BUS.addListener(NativeNauseaProbe::run);}
    static void state(HealthBridgeFixture peer,float previous,float current)throws Exception {
        var a=host.actors().getFirst();var bytes=peer.take(Wire.STATUS_NAUSEA).payload();check(bytes.length==48,"exact state size");
        var r=new Wire.Reader(bytes);check(r.i64()==host.epoch(),"current map epoch");long next=r.i64();check(next>revision,"revision advances");revision=next;
        check(r.i32()==a.slot()&&r.i32()==a.serial()&&r.i32()==a.spawn()&&r.i32()==a.life(),"full identity");
        float p=r.f32(),c=r.f32();check(Math.abs(p-previous)<.00002f&&Math.abs(c-current)<.00002f,"vanilla ramp/clear: "+p+","+c+" expected "+previous+","+current);
        float angle=r.f32(),step=r.f32();check(Float.isFinite(angle)&&Math.abs(angle)<=Math.PI+.000001,"wrapped phase");
        check(step>=0&&step<.35f,"angular step bound");r.finish();packets++;
    }
    static void exercise(HealthBridgeFixture peer)throws Exception {
        fresh();revision=0;var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive(),"real native proxy");state(peer,0,0);
        long t=world.getGameTime();float value=0;
        box.addEffect(new MobEffectInstance(MobEffects.CONFUSION,212));
        for(int i=0;i<151;i++){
            float old=value;value=Math.clamp(value+.006666667f,0,1);
            NativeStatusEffects.hud(a,box,null,++t);state(peer,old,value);
            step(box,1);
        }
        check(value==1,"full ramp clamps");
        NativeStatusEffects.hud(a,box,null,t);state(peer,1,1); // duplicate tick cannot advance
        check(box.getEffect(MobEffects.CONFUSION).getDuration()==61,"real countdown reaches 61");
        NativeStatusEffects.hud(a,box,null,++t);state(peer,1,1);
        for(int i=0;i<20;i++){
            step(box,1);float old=value;value=Math.max(0,value-.05f);
            NativeStatusEffects.hud(a,box,null,++t);state(peer,old,value);
        }
        box.removeEffect(MobEffects.CONFUSION);box.addEffect(new MobEffectInstance(MobEffects.CONFUSION,1000));
        NativeStatusEffects.hud(a,box,null,++t);state(peer,0,.006666667f); // removal/readd between publications
        NativeStatusEffects.hud(a,box,null,t);state(peer,0,.006666667f);
        box.removeAllEffects();NativeStatusEffects.hud(a,box,null,t);state(peer,0,0);

        box.addEffect(new MobEffectInstance(MobEffects.CONFUSION,61));
        NativeStatusEffects.hud(a,box,null,++t);state(peer,0,.006666667f);
        step(box,1);check(box.getEffect(MobEffects.CONFUSION).getDuration()==60,"real effect tick reached exactly 60");
        NativeStatusEffects.hud(a,box,null,++t);state(peer,.006666667f,0);
        box.removeEffect(MobEffects.CONFUSION);box.addEffect(new MobEffectInstance(MobEffects.CONFUSION,-1));
        NativeStatusEffects.hud(a,box,null,++t);state(peer,0,.006666667f); // infinite effect ramps
        box.removeEffectNoUpdate(MobEffects.CONFUSION);box.addEffect(new MobEffectInstance(MobEffects.CONFUSION,1000));
        NativeStatusEffects.hud(a,box,null,++t);state(peer,0,.006666667f); // internal removal resets too

        var receiver=player(new UUID(0,9061));NativeStatusEffects.transfer(box,receiver);
        check(receiver.hasEffect(MobEffects.CONFUSION)&&!box.hasEffect(MobEffects.CONFUSION),"unique effect owner moves");
        NativeNausea.update(a,receiver,++t);state(peer,.006666667f,.013333334f);
        NativeStatusEffects.transfer(receiver,box);NativeNausea.update(a,box,++t);state(peer,.013333334f,.02f);
        peer.authority(false);NativeNausea.update(a,box,++t);peer.authority(true);
        NativeNausea.update(a,box,++t);state(peer,0,.006666667f); // missed authority interval resets
        t+=20;NativeNausea.update(a,box,t);state(peer,0,.006666667f); // retired ticker cannot catch up
        NativeNausea.update(a,box,--t);state(peer,0,.006666667f); // clock reversal resets

        a=actor(NONE,3,7,12,1,100,0);snapshot(a,100);NativeNausea.update(a,box,++t);state(peer,0,.006666667f);
        a=actor(NONE,4,8,13,1,100,0);snapshot(a,100);NativeNausea.update(a,box,++t);state(peer,0,.006666667f);
        box.removeEffect(MobEffects.CONFUSION);box.addEffect(new MobEffectInstance(MobEffects.CONFUSION,1));
        step(box,1);check(!box.hasEffect(MobEffects.CONFUSION),"actual expiry callback");
        NativeNausea.update(a,box,++t);state(peer,0,0);
        fresh();revision=0;a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);state(peer,0,0);
    }
    static void run(ServerStartedEvent event){
        var result=new JsonObject();server=event.getServer();world=server.overworld();
        try{world.getChunk(0,0);bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(Wire.STATUS_NAUSEA_CAP)){
                check(GoldCraft.nauseaAvailable(),"real negotiated capability");exercise(peer);
            }
            result.addProperty("passed",true);
        }catch(Throwable failure){result.addProperty("passed",false);result.addProperty("error",failure.toString());failure.printStackTrace();}
        finally{result.addProperty("checks",checks);result.addProperty("packets",packets);cleanup();
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-nausea.json"),result.toString());}
            catch(Exception failure){throw new RuntimeException(failure);}
        }
    }
}
