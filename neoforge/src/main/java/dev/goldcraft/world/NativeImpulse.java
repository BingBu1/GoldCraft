package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.Entity;
import net.minecraft.util.math.Vec3d;

/** Forward the final vanilla/NeoForge explosion delta to the native movement owner. */
public final class NativeImpulse {
    private static long epoch,generation,sequence;
    private NativeImpulse(){}
    public static boolean explosion(Entity entity,Vec3d delta){
        return send(entity,delta,0,0,null);
    }
    static boolean confirmed(Entity entity,Vec3d delta,long damageEvent,long snapshotTick,dev.goldcraft.bridge.HostWorldState.Actor snapshot){
        return damageEvent!=0&&GoldCraft.nativeKnockbackAvailable()&&send(entity,delta,damageEvent,snapshotTick,snapshot);
    }
    private static boolean send(Entity entity,Vec3d delta,long damageEvent,long snapshotTick,dev.goldcraft.bridge.HostWorldState.Actor snapshot){
        NativePlayerEntity body=entity instanceof NativePlayerHitboxEntity box?box.body()
            :entity instanceof NativePlayerEntity player?player:null;
        if(body==null||body.isRemoved()||!body.isAlive()||entity.isRemoved()||entity.getWorld().isClient()
            ||!GoldCraft.nativeImpulseAvailable())return false;
        var host=GoldCraft.HOST_WORLD;var previous=body.actor();
        var actor=host.actors().stream().filter(a->a.slot()==previous.slot()).findFirst().orElse(null);
        if(body.epoch()!=host.epoch()||!entity.getWorld().getRegistryKey().getValue().toString().equals(host.dimension())
            ||actor==null||actor.serial()!=previous.serial()||actor.spawn()!=previous.spawn()||actor.life()!=previous.life()
            ||actor.minecraftForm()||(actor.flags()&5)!=1||actor.health()<=0||actor.life()==0
            ||(actor.team()!=1&&actor.team()!=2)||host.tick()==0||NativePlayers.hitbox(actor)!=body.hitbox())return false;
        // MC vectors are blocks/tick; GoldSrc uses units/second and a different up axis.
        double x=delta.x*640,y=-delta.z*640,z=delta.y*640;
        if(!Double.isFinite(x)||!Double.isFinite(y)||!Double.isFinite(z)
            ||Math.abs(x)>65536||Math.abs(y)>65536||Math.abs(z)>65536||(x==0&&y==0&&z==0))return false;
        long currentGeneration=GoldCraft.authorityGeneration();
        if(epoch!=host.epoch()||generation!=currentGeneration){epoch=host.epoch();generation=currentGeneration;sequence=0;}
        if(sequence==Long.MAX_VALUE)return false;
        var origin=snapshot==null?actor:snapshot;
        if(origin.slot()!=actor.slot()||origin.serial()!=actor.serial()||origin.spawn()!=actor.spawn()||origin.life()!=actor.life())return false;
        var w=new Wire.Writer().i64(epoch).i64(++sequence).i64(damageEvent==0?host.tick():snapshotTick)
            .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life());
        w.f32(origin.origin().x()+(origin.mins().x()+origin.maxs().x())*.5f)
            .f32(origin.origin().y()+(origin.mins().y()+origin.maxs().y())*.5f)
            .f32(origin.origin().z()+(origin.mins().z()+origin.maxs().z())*.5f)
            .f32((float)x).f32((float)y).f32((float)z);
        return damageEvent==0?GoldCraft.sendNativeImpulse(w.toByteArray()):GoldCraft.sendNativeKnockback(w.i64(damageEvent).toByteArray());
    }
}
