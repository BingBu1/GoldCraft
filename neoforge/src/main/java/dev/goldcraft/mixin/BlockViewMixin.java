package dev.goldcraft.mixin;

import dev.goldcraft.world.HostRaycast;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.world.BlockView;
import net.minecraft.world.RaycastContext;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(BlockView.class)
public interface BlockViewMixin {
    @Inject(method="raycast(Lnet/minecraft/world/RaycastContext;)Lnet/minecraft/util/hit/BlockHitResult;",at=@At("RETURN"),cancellable=true)
    default void goldcraft$hostRaycast(RaycastContext context,CallbackInfoReturnable<BlockHitResult> result) {
        if((Object)this instanceof World world)result.setReturnValue(HostRaycast.closest(world,context,result.getReturnValue()));
    }
}
