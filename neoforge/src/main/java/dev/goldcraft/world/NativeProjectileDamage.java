package dev.goldcraft.world;

import net.minecraft.entity.Entity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.damage.DamageType;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.DamageTypeTags;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.util.Identifier;
import net.minecraft.util.hit.EntityHitResult;
import net.minecraft.util.math.Vec3d;

/** Keeps the actual server ray hit while a fast projectile is still at its tick start. */
public final class NativeProjectileDamage {
    private static final TagKey<DamageType> PROJECTILE=TagKey.of(RegistryKeys.DAMAGE_TYPE,Identifier.of("goldcraft","projectile_attack"));
    private record Hit(Entity projectile,Entity target,Vec3d point) {}
    private static final ThreadLocal<Hit> ACTIVE=new ThreadLocal<>();
    private NativeProjectileDamage() {}

    public static boolean isProjectile(DamageSource source) {
        return source.isIn(DamageTypeTags.IS_PROJECTILE)||source.isIn(PROJECTILE);
    }

    /** Optional gun integration; ordinary MC targets keep the Mod's original path. */
    public static void hit(Entity projectile,EntityHitResult result,Vec3d start,Runnable original) {
        Entity target=result.getEntity();
        if(!(target instanceof NativePlayerHitboxEntity)||projectile.getWorld().isClient
                ||target.getWorld()!=projectile.getWorld()) { original.run();return; }
        Vec3d point=result.getPos();
        if(!finite(start)||!finite(point))return;
        // TACZ's own block walker does not call BlockView.raycast. Keep the
        // complete bullet segment checked against the authoritative host BSP.
        var host=HostCollision.state(projectile.getWorld());
        if(host!=null&&HostRaycast.trace(host,start,point)!=null)return;
        Hit previous=ACTIVE.get();ACTIVE.set(new Hit(projectile,target,point));
        try { original.run(); }
        finally { if(previous==null)ACTIVE.remove();else ACTIVE.set(previous); }
    }

    static Vec3d contact(NativePlayerEntity target,DamageSource source) {
        var hit=ACTIVE.get();
        return hit!=null&&hit.projectile()==source.getSource()&&hit.target()==target.hitbox()?hit.point():null;
    }

    private static boolean finite(Vec3d point) {
        return Double.isFinite(point.x)&&Double.isFinite(point.y)&&Double.isFinite(point.z);
    }
}
