package dev.goldcraft.mixin;

import dev.goldcraft.world.HostCollision;
import net.minecraft.block.BlockState;
import net.minecraft.fluid.FlowableFluid;
import net.minecraft.fluid.Fluid;
import net.minecraft.fluid.FluidState;
import net.minecraft.block.FluidBlock;
import net.minecraft.block.Blocks;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.BlockView;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Vanilla liquid cells cannot contain a fractional BSP obstacle. Reserve every
 * intersected cell, including thin interior walls, without writing fake blocks.
 * Run before vanilla's state-only cache: identical air states can have different
 * BSP geometry, and doors/mined cuts change without any MC block-state update. */
@Mixin(FlowableFluid.class)
public abstract class HostFluidMixin {
    @Inject(method="onScheduledTick",at=@At("HEAD"),cancellable=true)
    private void goldcraft$retireEmbedded(World world,BlockPos pos,FluidState state,CallbackInfo ci){
        if(world.getBlockState(pos).getBlock() instanceof FluidBlock&&!HostCollision.shape(world,pos).isEmpty()){
            // Retire old invalid spill cells through normal neighbor updates;
            // never replace waterlogged building blocks or write fake terrain.
            world.setBlockState(pos,Blocks.AIR.getDefaultState(),3);ci.cancel();
        }
    }
    @Inject(method="receivesFlow",at=@At("HEAD"),cancellable=true)
    private void goldcraft$barrier(Direction face,BlockView world,BlockPos pos,BlockState state,
                                  BlockPos fromPos,BlockState fromState,CallbackInfoReturnable<Boolean> result){
        if(!HostCollision.shape(world,pos).isEmpty()||!HostCollision.shape(world,fromPos).isEmpty())
            result.setReturnValue(false);
    }

    @Inject(method="canFill",at=@At("HEAD"),cancellable=true)
    private void goldcraft$fill(BlockView world,BlockPos pos,BlockState state,Fluid fluid,
                               CallbackInfoReturnable<Boolean> result){
        if(!HostCollision.shape(world,pos).isEmpty())result.setReturnValue(false);
    }
}
