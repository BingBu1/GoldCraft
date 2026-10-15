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
    private NativeTntDamage() {}

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

    public static HostWorldState.Actor owner(HostWorldState host,Entity attacker) {
        HostWorldState.Actor candidate=null;
        if(attacker instanceof NativePlayerEntity p&&p.epoch()==host.epoch())candidate=p.actor();
        else if(attacker instanceof ServerPlayerEntity p&&p.getServer()!=null
            &&p.getServer().getPlayerManager().getPlayer(p.getUuid())==p)candidate=host.actor(p.getUuid());
        if(candidate==null)return null;
        for(var current:host.actors())if(current.slot()==candidate.slot()&&current.serial()==candidate.serial()
            &&current.spawn()==candidate.spawn())return current;
        // A disconnected/replaced owner cannot take credit through a reused slot.
        return null;
    }
}
