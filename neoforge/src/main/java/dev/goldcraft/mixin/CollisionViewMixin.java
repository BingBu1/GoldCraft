package dev.goldcraft.mixin;

import dev.goldcraft.world.HostCollision;
import net.minecraft.entity.Entity;
import net.minecraft.util.math.Box;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.world.CollisionView;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(CollisionView.class)
public interface CollisionViewMixin {
    @Inject(method="getBlockCollisions",at=@At("RETURN"),cancellable=true)
    default void goldcraft$hostCollision(Entity entity,Box box,CallbackInfoReturnable<Iterable<VoxelShape>> result) {
        World world=HostCollision.world((CollisionView)(Object)this);
        if(world!=null)result.setReturnValue(HostCollision.augment(world,box,result.getReturnValue()));
    }
}
