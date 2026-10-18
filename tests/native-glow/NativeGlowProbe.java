package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.*;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;
import net.minecraft.ChatFormatting;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.UUID;

/** Real FML entities/effects/scoreboards and the shipped publisher over real TCP. */
@Mod("goldcraft_native_glow_probe")
public final class NativeGlowProbe {
    static int checks,packets;static long revision;
    static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    public NativeGlowProbe(){NeoForge.EVENT_BUS.addListener(NativeGlowProbe::run);}
    static void glow(HealthBridgeFixture peer,boolean enabled,int color)throws Exception {
        var a=host.actors().getFirst();var bytes=peer.take(Wire.STATUS_GLOW).payload();check(bytes.length==40,"exact state size");
        var r=new Wire.Reader(bytes);check(r.i64()==host.epoch(),"current map epoch");long next=r.i64();check(next>revision,"producer revision advances");revision=next;
        check(r.i32()==a.slot()&&r.i32()==a.serial()&&r.i32()==a.spawn()&&r.i32()==a.life(),"full current actor identity");
        check(r.i32()==(enabled?1:0)&&r.i32()==color,"actual glow flag and team color");r.finish();packets++;
    }
    static void exercise(HealthBridgeFixture peer)throws Exception {
        fresh();revision=0;var a=actor(NONE,3,7,11,1,100,0);snapshot(a,100);NativePlayers.tick(server,host);
        var box=NativePlayers.hitbox(a);check(box!=null&&box.isAlive()&&box.isInvisible(),"actual native proxy is permanently hidden");
        glow(peer,false,0);
        box.addEffect(new MobEffectInstance(MobEffects.GLOWING,3));NativeStatusEffects.hud(a,box,null,1);glow(peer,true,0xffffff);
        var team=world.getScoreboard().addPlayerTeam("native_glow_probe");team.setColor(ChatFormatting.RED);
        world.getScoreboard().addPlayerToTeam(box.getScoreboardName(),team);
        NativeStatusEffects.hud(a,box,null,2);glow(peer,true,0xff5555);
        step(box,3);check(!box.hasEffect(MobEffects.GLOWING),"actual effect expires on proxy tick");
        NativeStatusEffects.hud(a,box,null,3);glow(peer,false,0);
        box.setGlowingTag(true);check(!box.hasEffect(MobEffects.GLOWING),"command glow without potion");
        NativeStatusEffects.hud(a,box,null,4);glow(peer,true,0xff5555);
        box.setGlowingTag(false);NativeStatusEffects.hud(a,box,null,5);glow(peer,false,0);

        fresh();revision=0;var p=player(new UUID(0,9055));a=actor(p.getUUID(),4,8,12,113,100,0);snapshot(a,100);
        var blue=world.getScoreboard().addPlayerTeam("paired_glow_probe");blue.setColor(ChatFormatting.BLUE);
        world.getScoreboard().addPlayerToTeam(p.getScoreboardName(),blue);
        p.addEffect(new MobEffectInstance(MobEffects.GLOWING,100));NativePlayers.tick(server,host);glow(peer,true,0x5555ff);
        a=actor(p.getUUID(),4,8,13,17,100,0);snapshot(a,100);NativePlayers.tick(server,host);box=NativePlayers.hitbox(a);
        check(box!=null&&box.hasEffect(MobEffects.GLOWING)&&!p.hasEffect(MobEffects.GLOWING),"one effect owner after MC to CS handoff");glow(peer,true,0x5555ff);
        box.removeEffect(MobEffects.GLOWING);p.setGlowingTag(true);NativeStatusEffects.hud(a,box,p,6);glow(peer,true,0x5555ff);
        p.setGlowingTag(false);NativeStatusEffects.hud(a,box,p,7);glow(peer,false,0);
        box.addEffect(new MobEffectInstance(MobEffects.GLOWING,100));
        var wrong=player(new UUID(0,9056));wrong.setGlowingTag(true);
        NativeGlow.update(a,box,wrong);glow(peer,true,0xffffff); // another player cannot supply color
        a=actor(p.getUUID(),4,8,14,113,100,0);snapshot(a,100);NativePlayers.tick(server,host);glow(peer,true,0x5555ff);
        check(p.hasEffect(MobEffects.GLOWING)&&box.isRemoved(),"CS to MC returns sole effect owner");
        p.removeEffect(MobEffects.GLOWING);NativeStatusEffects.hud(a,p,p,8);glow(peer,false,0);
        peer.authority(false);check(!GoldCraft.glowAvailable(),"authority withdrawal disables publisher");
        NativeGlow.update(a,p,p);peer.authority(true);p.addEffect(new MobEffectInstance(MobEffects.GLOWING,100));
        NativeGlow.update(a,p,p);glow(peer,true,0x5555ff); // no disabled-authority packet can precede this one
    }
    static void run(ServerStartedEvent event){
        var result=new JsonObject();server=event.getServer();world=server.overworld();
        try{world.getChunk(0,0);bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(Wire.STATUS_GLOW_CAP)){
                check(GoldCraft.glowAvailable(),"real negotiated capability");exercise(peer);
            }
            result.addProperty("passed",true);
        }catch(Throwable failure){result.addProperty("passed",false);result.addProperty("error",failure.toString());failure.printStackTrace();}
        finally{result.addProperty("checks",checks);result.addProperty("packets",packets);cleanup();
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("native-glow.json"),result.toString());}
            catch(Exception failure){throw new RuntimeException(failure);}
        }
    }
}
