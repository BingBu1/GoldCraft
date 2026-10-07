package dev.goldcraft.mixin;

import dev.goldcraft.world.HostCollision;
import net.minecraft.entity.ai.NavigationConditions;
import net.minecraft.entity.mob.PathAwareEntity;
import net.minecraft.util.math.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(NavigationConditions.class)
public abstract class NavigationConditionsMixin {
    @Inject(method="isSolidAt",at=@At("RETURN"),cancellable=true)
    private static void goldcraft$obstacle(PathAwareEntity entity,BlockPos pos,CallbackInfoReturnable<Boolean> result){
        if(!result.getReturnValue()&&!HostCollision.shape(entity.getWorld(),pos).isEmpty())result.setReturnValue(true);
    }
}
