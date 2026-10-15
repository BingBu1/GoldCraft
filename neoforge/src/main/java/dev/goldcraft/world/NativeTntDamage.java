package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.TntEntity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.registry.tag.DamageTypeTags;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;
import net.minecraft.world.explosion.Explosion;
import java.util.function.BooleanSupplier;

/** Scopes a real vanilla TNT hurt call; a damage tag alone grants no authority. */
public final class NativeTntDamage {
    private record Hit(Entity target,DamageSource source,Vec3d center) {}
    private static final ThreadLocal<Hit> ACTIVE=new ThreadLocal<>();
    private static final ThreadLocal<TntEntity> EXPLODING=new ThreadLocal<>();
    private NativeTntDamage() {}

    public static NativeTntOrigin capture(TntEntity tnt,Entity igniter) {
        var host=GoldCraft.HOST_WORLD;
        if(tnt.getWorld().isClient()||!tnt.getWorld().getRegistryKey().getValue().toString().equals(host.dimension()))return null;
        var parent=EXPLODING.get();
        // A chain keeps the original identity, including a missing/expired owner.
        // Looking up the same player again here could grant a new spawn credit.
        if(parent!=null&&parent.getWorld()==tnt.getWorld()&&parent.getOwner()==igniter)
            return ((TntOwnerAccess)parent).goldcraft$owner();
        HostWorldState.Actor actor=null;
        java.util.UUID paired=null;
        if(igniter instanceof NativePlayerEntity p&&p.epoch()==host.epoch())actor=p.actor();
        else if(igniter instanceof ServerPlayerEntity p&&p.getServer()!=null
            &&p.getServer().getPlayerManager().getPlayer(p.getUuid())==p) {
            actor=host.actor(p.getUuid());paired=p.getUuid();
        }
        if(actor==null)return null;
        var origin=new NativeTntOrigin(host.epoch(),actor.slot(),actor.serial(),actor.spawn(),paired);
        return origin.resolve(host)==null?null:origin;
    }

    public static void explode(TntEntity tnt,Runnable original) {
        var previous=EXPLODING.get();EXPLODING.set(tnt);
        try{original.run();}
        finally{if(previous==null)EXPLODING.remove();else EXPLODING.set(previous);}
    }

    public static boolean hurt(World world,Explosion explosion,Entity target,DamageSource source,BooleanSupplier original) {
        if(world.isClient()||!(explosion.getEntity() instanceof TntEntity)
            ||source.getSource()!=explosion.getEntity()||!source.isIn(DamageTypeTags.IS_EXPLOSION)
            ||!world.getRegistryKey().getValue().toString().equals(GoldCraft.HOST_WORLD.dimension()))return original.getAsBoolean();
        Hit previous=ACTIVE.get();ACTIVE.set(new Hit(target,source,explosion.getPosition()));
        try{return original.getAsBoolean();}
        finally{if(previous==null)ACTIVE.remove();else ACTIVE.set(previous);}
    }

    public static Vec3d center(NativePlayerEntity target,DamageSource source) {
        var hit=ACTIVE.get();
        return hit!=null&&hit.source()==source&&(hit.target()==target||hit.target()==target.hitbox())?hit.center():null;
    }

    public static HostWorldState.Actor owner(HostWorldState host,DamageSource source) {
        if(!(source.getSource() instanceof TntEntity tnt))return null;
        var origin=((TntOwnerAccess)tnt).goldcraft$owner();
        return origin==null?null:origin.resolve(host);
    }
}
