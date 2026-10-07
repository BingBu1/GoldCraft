package dev.goldcraft.mixin;

import dev.goldcraft.world.HostMovement;
import net.minecraft.entity.LivingEntity;
import net.minecraft.util.math.BlockPos;
import java.util.Optional;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LivingEntity.class)
public abstract class LivingLadderMixin {
    @Shadow private Optional<BlockPos> climbingPos;
    @Inject(method="isClimbing",at=@At("RETURN"),cancellable=true)
    private void goldcraft$hostLadder(CallbackInfoReturnable<Boolean> result) {
        LivingEntity entity=(LivingEntity)(Object)this;
        if(!result.getReturnValueZ()&&HostMovement.ladder(entity)!=null) {
            climbingPos=Optional.of(entity.getBlockPos());result.setReturnValue(true);
        }
    }
}
