package dev.goldcraft.mixin;

import dev.goldcraft.world.HostMovement;
import net.minecraft.entity.EntityPose;
import net.minecraft.entity.player.PlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(PlayerEntity.class)
public abstract class PlayerPoseMixin {
    @Inject(method="canChangeIntoPose",at=@At("RETURN"),cancellable=true)
    private void goldcraft$clipHeadroom(EntityPose pose,CallbackInfoReturnable<Boolean> result) {
        if(result.getReturnValueZ()&&!HostMovement.canPose((PlayerEntity)(Object)this,pose))result.setReturnValue(false);
    }
}
