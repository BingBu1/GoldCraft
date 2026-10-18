package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.NativePlayers;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.phys.Vec3;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.level.ExplosionEvent;
import net.neoforged.neoforge.event.level.ExplosionKnockbackEvent;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.TimeUnit;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;

/** Actual wind/TNT explosions and final Mod knockback events; the host peer is a fixture. */
@Mod("goldcraft_native_impulse_probe")
public final class NativeImpulseProbe {
    static int checks,events;static boolean cancel,prune;static Vec3 override,observed;
    static NativePlayerHitboxEntity target;
    static void check(boolean b,String name){checks++;if(!b)throw new AssertionError(name);}
    public NativeImpulseProbe(){
        NeoForge.EVENT_BUS.addListener(NativeImpulseProbe::run);
        NeoForge.EVENT_BUS.addListener((ExplosionEvent.Start e)->{if(cancel)e.setCanceled(true);});
        NeoForge.EVENT_BUS.addListener((ExplosionEvent.Detonate e)->{if(prune)e.getAffectedEntities().remove(target);});
        NeoForge.EVENT_BUS.addListener((ExplosionKnockbackEvent e)->{
            if(e.getAffectedEntity()==target){events++;if(override!=null)e.setKnockbackVelocity(override);observed=e.getKnockbackVelocity();}
        });
    }
    static void burst() {
        LivingEntity source=EntityType.ZOMBIE.create(world);source.setPos(2,70,0);world.addFreshEntity(source);
        source.addEffect(new MobEffectInstance(MobEffects.WIND_CHARGED,100));
        source.remove(Entity.RemovalReason.KILLED);
    }
    static float[] impulse(HealthBridgeFixture peer)throws Exception{
        var r=new Wire.Reader(peer.take(101).payload());
        check(r.i64()==host.epoch()&&r.i64()>0&&r.i64()==host.tick(),"world/event/snapshot");
        check(r.i32()==2&&r.i32()==3&&r.i32()==7&&r.i32()==11,"complete native identity");
        r.f32();r.f32();r.f32();float[] v={r.f32(),r.f32(),r.f32()};r.finish();return v;
    }
    static void empty(HealthBridgeFixture peer)throws Exception{
        Thread.sleep(80);var q=peer.messages.get(101);check(q==null||q.isEmpty(),"no rejected impulse");
    }
    static void matches(float[] v,Vec3 expected){
        check(Math.abs(v[0]-expected.x*640)<.001&&Math.abs(v[1]+expected.z*640)<.001&&Math.abs(v[2]-expected.y*640)<.001,"final event vector and axis/time conversion");
    }
    static void exercise(HealthBridgeFixture peer)throws Exception{
        fresh();var a=actor(NONE,3,7,11,1,1000,0);snapshot(a,200);NativePlayers.tick(server,host);target=NativePlayers.hitbox(a);
        check(target!=null,"native recipient");
        for(int x=-5;x<=5;x++)for(int z=-5;z<=5;z++)for(int y=69;y<=74;y++)world.setBlock(new BlockPos(x,y,z),y==69?Blocks.STONE.defaultBlockState():Blocks.AIR.defaultBlockState(),3);
        events=0;burst();check(events==1&&observed.length()>0,"real wind explosion computes one impulse");matches(impulse(peer),observed);
        check(target.getHealth()==200,"damage-free wind preserves native health");
        override=new Vec3(.25,.5,-.75);burst();matches(impulse(peer),override);
        target.setDeltaMovement(new Vec3(9,8,7));burst();matches(impulse(peer),override);check(target.getDeltaMovement().equals(new Vec3(9.25,8.5,6.25)),"original local velocity semantics retained");
        override=Vec3.ZERO;burst();empty(peer);override=null;
        cancel=true;events=0;burst();check(events==0,"Start cancellation skips impulse");empty(peer);cancel=false;
        prune=true;burst();empty(peer);prune=false;
        target.getAttribute(Attributes.EXPLOSION_KNOCKBACK_RESISTANCE).setBaseValue(1);burst();check(observed.length()==0,"vanilla explosion resistance");empty(peer);
        target.getAttribute(Attributes.EXPLOSION_KNOCKBACK_RESISTANCE).setBaseValue(0);
        override=new Vec3(.1,.2,.3);world.explode(null,2,70,0,2,Level.ExplosionInteraction.NONE);matches(impulse(peer),override);
        snapshot(actor(NONE,3,7,12,1,1000,0),200);burst();empty(peer);
        snapshot(a,200);peer.authority(false);burst();empty(peer);peer.authority(true);
        // A normal MC entity still receives the engine's velocity without native packets.
        var pig=EntityType.PIG.create(world);pig.setPos(0,70,2);world.addFreshEntity(pig);
        snapshot(null,200);burst();check(pig.getDeltaMovement().length()>0,"ordinary MC explosion behavior retained");empty(peer);pig.discard();
        override=null;
    }
    static void run(ServerStartedEvent event){
        var result=new JsonObject();
        try{
            server=event.getServer();world=server.overworld();bsp=Files.readAllBytes(Path.of("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(Boolean.getBoolean("goldcraft.impulse.baseline")?0:33554432)){exercise(peer);}
            result.addProperty("passed",true);
        }catch(Throwable e){result.addProperty("passed",false);result.addProperty("error",e.toString());e.printStackTrace();}
        finally{try{cleanup();}catch(Throwable e){result.addProperty("cleanupError",e.toString());}result.addProperty("checks",checks);try{Files.writeString(Path.of("native-impulse.json"),result.toString());}catch(Exception e){throw new RuntimeException(e);}}
    }
}
