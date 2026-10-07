package dev.goldcraft.mixin;

import dev.goldcraft.world.HostMovement;
import net.minecraft.entity.Entity;
import net.minecraft.util.math.Vec3d;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Entity.class)
public abstract class EntityMovementMixin {
    @Inject(method="adjustMovementForCollisions(Lnet/minecraft/util/math/Vec3d;)Lnet/minecraft/util/math/Vec3d;",at=@At("RETURN"),cancellable=true)
    private void goldcraft$playerClip(Vec3d requested,CallbackInfoReturnable<Vec3d> result) {
        result.setReturnValue(HostMovement.adjust((Entity)(Object)this,requested,result.getReturnValue()));
    }
}
