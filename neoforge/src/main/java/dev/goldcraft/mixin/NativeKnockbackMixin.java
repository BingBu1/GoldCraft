package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.goldcraft.world.NativeKnockback;
import net.minecraft.entity.LivingEntity;
import net.minecraft.util.math.Vec3d;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** Capture the original calculation after NeoForge cancellation and resistance. */
@Mixin(LivingEntity.class)
public abstract class NativeKnockbackMixin {
    @WrapOperation(method="takeKnockback",at=@At(value="INVOKE",target="Lnet/minecraft/util/math/Vec3d;multiply(D)Lnet/minecraft/util/math/Vec3d;"),require=1,allow=1)
    private Vec3d goldcraft$shape(Vec3d direction,double strength,Operation<Vec3d> original){
        Vec3d result=original.call(direction,strength);
        NativeKnockback.shape((LivingEntity)(Object)this,strength,result);
        return result;
    }
    @WrapOperation(method="takeKnockback",at=@At(value="INVOKE",target="Lnet/minecraft/entity/LivingEntity;setVelocity(DDD)V"),require=1,allow=1)
    private void goldcraft$velocity(LivingEntity entity,double x,double y,double z,Operation<Void> original){
        NativeKnockback.velocity(entity,new Vec3d(x,y,z));
        original.call(entity,x,y,z);
    }
}
