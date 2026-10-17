package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import dev.goldcraft.world.NativeProjectileDamage;
import net.minecraft.entity.Entity;
import net.minecraft.util.hit.EntityHitResult;
import net.minecraft.util.math.Vec3d;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.Coerce;

/** TACZ 1.1.8 supplies the hit point before advancing the projectile position. */
@Pseudo
@Mixin(targets="com.tacz.guns.entity.EntityKineticBullet",remap=false)
abstract class TaczProjectileHitMixin {
    @WrapMethod(method="onHitEntity")
    private void goldcraft$projectileHit(@Coerce Object result,Vec3d start,Vec3d end,Operation<Void> original) {
        NativeProjectileDamage.hit((Entity)(Object)this,(EntityHitResult)result,start,()->original.call(result,start,end));
    }
}
