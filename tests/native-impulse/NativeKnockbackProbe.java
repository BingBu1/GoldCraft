package dev.goldcraft.healthprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.*;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.phys.Vec3;
import net.neoforged.fml.common.Mod;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.living.LivingIncomingDamageEvent;
import net.neoforged.neoforge.event.entity.living.LivingKnockBackEvent;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.nio.file.Path;
import static dev.goldcraft.healthprobe.HealthWorldFixture.*;

/** Real vanilla hurt/mob attack/event pipeline. Host snapshots and receipts are fixtures. */
@Mod("goldcraft_native_knockback_probe")
public final class NativeKnockbackProbe {
    static int checks,events;
    static boolean cancelHurt,cancelKnockback;
    static float strength=-1;
    static NativePlayerHitboxEntity target;
    static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    public NativeKnockbackProbe(){
        NeoForge.EVENT_BUS.addListener(NativeKnockbackProbe::run);
        NeoForge.EVENT_BUS.addListener((LivingIncomingDamageEvent e)->{if(cancelHurt&&e.getEntity()==target)e.setCanceled(true);});
        NeoForge.EVENT_BUS.addListener((LivingKnockBackEvent e)->{
            if(e.getEntity()==target){events++;if(cancelKnockback)e.setCanceled(true);if(strength>=0)e.setStrength(strength);}
        });
    }
    static void movement(boolean ground)throws Exception{
        var a=host.actors().getFirst();
        var w=new Wire.Writer().i64(host.epoch()).i64(host.tick()).i32(1).i32(a.slot()).i32(a.serial()).i32(a.spawn()).i32(a.life())
            .f32(320).f32(-640).f32(128).i32(ground?3:2);
        check(host.nativeMovement().accept(host,w.toByteArray(),server.getTickCount()),"movement accepted");
    }
    static long receipt(Wire.Message m,int status)throws Exception{
        var r=new Wire.Reader(m.payload());long epoch=r.i64(),event=r.i64();r.i64();int slot=r.i32(),serial=r.i32(),spawn=r.i32();
        NativePlayers.result(new Wire.Writer().i64(epoch).i64(event).i32(slot).i32(serial).i32(spawn).i32(status).f32(1000).f32(0).toByteArray());return event;
    }
    static void empty(HealthBridgeFixture peer)throws Exception{
        Thread.sleep(60);var q=peer.messages.get(Wire.NATIVE_KNOCKBACK);check(q==null||q.isEmpty(),"no unauthorized impulse");
    }
    static void impulse(HealthBridgeFixture peer,long event,Vec3 expected)throws Exception{
        var r=new Wire.Reader(peer.take(Wire.NATIVE_KNOCKBACK).payload());
        check(r.i64()==host.epoch()&&r.i64()>0&&r.i64()==host.tick(),"impulse epoch/event/tick");
        check(r.i32()==2&&r.i32()==3&&r.i32()==7&&r.i32()==11,"impulse identity");
        check(r.f32()==0&&r.f32()==0&&r.f32()==228,"original snapshot center");
        double x=r.f32()/640.0,y=r.f32()/640.0,z=r.f32()/640.0;
        check(Math.abs(x-expected.x)<1e-6&&Math.abs(y+expected.z)<1e-6&&Math.abs(z-expected.y)<1e-6,"vanilla impulse calculation "+new Vec3(x,z,-y)+" expected "+expected);
        check(r.i64()==event,"matching damage event");r.finish();empty(peer);
    }
    static void exercise(HealthBridgeFixture peer)throws Exception{
        fresh();var a=actor(NONE,3,7,11,1,1000,0);snapshot(a,200);movement(true);NativePlayers.tick(server,host);target=NativePlayers.hitbox(a);
        var mob=EntityType.ZOMBIE.create(world);mob.setPos(1,70,0);world.addFreshEntity(mob);
        MinecraftObjects.tick(server,host);peer.take(Wire.MINECRAFT_OBJECTS);
        var source=world.damageSources().mobAttack(mob);Vec3 sentinel=new Vec3(9,8,7);target.setDeltaMovement(sentinel);
        check(target.hurt(source,2),"ordinary hurt starts");var request=peer.take(Wire.DAMAGE_REQUEST);empty(peer);
        check(events==1&&target.getDeltaMovement().equals(sentinel),"one real event with temporary authoritative motion");
        impulse(peer,receipt(request,0),new Vec3(-.65,.2,-.5));receipt(request,0);empty(peer);
        target.invulnerableTime=0;check(target.hurt(source,2),"rejected host request starts");request=peer.take(Wire.DAMAGE_REQUEST);receipt(request,4);empty(peer);
        snapshot(a,200);movement(false);NativePlayers.tick(server,host);target=NativePlayers.hitbox(a);target.invulnerableTime=0;
        check(target.hurt(source,2),"air hurt");request=peer.take(Wire.DAMAGE_REQUEST);impulse(peer,receipt(request,0),new Vec3(-.65,0,-.5));
        cancelKnockback=true;target.invulnerableTime=0;check(target.hurt(source,2),"cancelled knockback still hurts");request=peer.take(Wire.DAMAGE_REQUEST);receipt(request,0);empty(peer);cancelKnockback=false;
        cancelHurt=true;target.invulnerableTime=0;check(!target.hurt(source,2),"hurt veto preserved");empty(peer);cancelHurt=false;
        target.getAttribute(Attributes.KNOCKBACK_RESISTANCE).setBaseValue(1);target.invulnerableTime=0;
        check(target.hurt(source,2),"resistant target hurt");request=peer.take(Wire.DAMAGE_REQUEST);receipt(request,0);empty(peer);
        target.getAttribute(Attributes.KNOCKBACK_RESISTANCE).setBaseValue(0);strength=.2f;target.invulnerableTime=0;
        check(target.hurt(source,2),"modified event hurt");request=peer.take(Wire.DAMAGE_REQUEST);impulse(peer,receipt(request,0),new Vec3(-.45,0,-.5));strength=-1;
        snapshot(a,200);movement(true);NativePlayers.tick(server,host);target=NativePlayers.hitbox(a);target.invulnerableTime=0;
        mob.getAttribute(Attributes.ATTACK_KNOCKBACK).setBaseValue(1);mob.setYRot(90);events=0;
        check(mob.doHurtTarget(target.body()),"actual mob attack succeeds");request=peer.take(Wire.DAMAGE_REQUEST);empty(peer);
        check(events==2,"hurt and attacker additional knockback both execute");impulse(peer,receipt(request,0),new Vec3(-1.075,.2,-.75));
        target.invulnerableTime=0;check(target.hurt(source,2),"old life hurt");request=peer.take(Wire.DAMAGE_REQUEST);
        snapshot(actor(NONE,3,8,12,1,1000,0),200);movement(true);NativePlayers.tick(server,host);receipt(request,0);empty(peer);
        mob.discard();
    }
    static void run(ServerStartedEvent e){
        var result=new JsonObject();server=e.getServer();world=server.overworld();
        try{bsp=Files.readAllBytes(Path.of("cs_assault.bsp"));
            try(var peer=new HealthBridgeFixture(Wire.NATIVE_KNOCKBACK_CAP|Wire.NATIVE_MOVEMENT_CAP|Wire.NATIVE_IMPULSE_CAP)){exercise(peer);}
            result.addProperty("passed",true);
        }catch(Throwable error){result.addProperty("passed",false);result.addProperty("error",error.toString());error.printStackTrace();}
        finally{cleanup();result.addProperty("checks",checks);try{Files.writeString(Path.of("native-knockback.json"),result.toString());}catch(Exception error){throw new RuntimeException(error);}}
    }
}
