package dev.goldcraft.mixin;

import dev.goldcraft.world.HostNavigation;
import net.minecraft.block.BlockState;
import net.minecraft.entity.ai.pathing.EntityNavigation;
import net.minecraft.entity.ai.pathing.MobNavigation;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import net.minecraft.world.chunk.WorldChunk;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(MobNavigation.class)
public abstract class MobNavigationMixin extends EntityNavigation {
    protected MobNavigationMixin(MobEntity entity,World world){super(entity,world);}

    @Redirect(method="findPathTo(Lnet/minecraft/util/math/BlockPos;I)Lnet/minecraft/entity/ai/pathing/Path;",at=@At(value="INVOKE",target="Lnet/minecraft/world/chunk/WorldChunk;getBlockState(Lnet/minecraft/util/math/BlockPos;)Lnet/minecraft/block/BlockState;"))
    private BlockState goldcraft$targetSupport(WorldChunk chunk,BlockPos pos){
        return HostNavigation.scanState(world,pos,chunk.getBlockState(pos));
    }

    @Inject(method="getPathfindingY",at=@At("RETURN"),cancellable=true)
    private void goldcraft$positionY(CallbackInfoReturnable<Integer> result){
        result.setReturnValue(HostNavigation.startY(entity,result.getReturnValue()));
    }
}
