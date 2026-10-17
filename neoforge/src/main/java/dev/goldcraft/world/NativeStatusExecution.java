package dev.goldcraft.world;

import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.Vec3d;

/** Only actual server effect callbacks open this scope; unrelated hurts stay ordinary. */
public final class NativeStatusExecution implements AutoCloseable {
    private static final ThreadLocal<NativeStatusExecution> CURRENT=new ThreadLocal<>();
    private final NativeStatusExecution previous;
    public final LivingEntity target;
    public final NativeStatusOrigin origin;
    public final boolean periodic;
    public final Vec3d contact;
    public final boolean poison;
    public final boolean traceContact;
    private NativeStatusExecution(LivingEntity target,NativeStatusOrigin origin,boolean periodic,Vec3d contact,boolean poison,boolean traceContact){
        this.previous=CURRENT.get();this.target=target;this.origin=origin;this.periodic=periodic;this.contact=contact;this.poison=poison;
        this.traceContact=traceContact;
        CURRENT.set(this);
    }
    public static NativeStatusExecution effect(LivingEntity target,StatusEffectInstance effect,boolean periodic){
        return open(target,((NativeStatusInstance)(Object)effect).goldcraft$statusOrigin(),periodic,target.getBoundingBox().getCenter(),effect.getEffectType().equals(StatusEffects.POISON),false);
    }
    public static NativeStatusExecution instant(LivingEntity target,Entity direct,Entity attacker){
        return open(target,NativeStatusOrigin.capture(attacker),false,direct==null?target.getBoundingBox().getCenter():direct.getPos(),false,direct!=null);
    }
    public static NativeStatusExecution nutrition(NativePlayerHitboxEntity target){
        return open(target,null,true,target.getBoundingBox().getCenter(),false,false);
    }
    private static NativeStatusExecution open(LivingEntity target,NativeStatusOrigin origin,boolean periodic,Vec3d contact,boolean poison,boolean traceContact){
        return target instanceof NativePlayerHitboxEntity&&target.getWorld() instanceof ServerWorld
            ?new NativeStatusExecution(target,origin,periodic,contact,poison,traceContact):null;
    }
    public static NativeStatusExecution current(LivingEntity target){
        var active=CURRENT.get();return active!=null&&active.target==target?active:null;
    }
    @Override public void close(){if(previous==null)CURRENT.remove();else CURRENT.set(previous);}
}
