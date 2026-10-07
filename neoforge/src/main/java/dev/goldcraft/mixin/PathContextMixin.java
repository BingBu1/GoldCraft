package dev.goldcraft.mixin;

import dev.goldcraft.world.HostCollision;
import net.minecraft.entity.ai.pathing.PathContext;
import net.minecraft.entity.ai.pathing.PathNodeType;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.CollisionView;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(PathContext.class)
public abstract class PathContextMixin {
    @Shadow @Final private CollisionView world;

    @Inject(method="getNodeType",at=@At("HEAD"),cancellable=true)
    private void goldcraft$hostNode(int x,int y,int z,CallbackInfoReturnable<PathNodeType> result){
        // Host brushes move without vanilla block updates. Never put their state in
        // ServerWorld's long-lived block node cache.
        if(!HostCollision.shape(world,new BlockPos(x,y,z)).isEmpty())result.setReturnValue(PathNodeType.BLOCKED);
    }
}
