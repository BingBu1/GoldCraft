package dev.goldcraft.mixin;

import dev.goldcraft.world.HostBlockSupport;
import net.minecraft.block.BlockState;
import net.minecraft.block.FallingBlock;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.random.Random;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(FallingBlock.class)
public abstract class HostFallingBlockMixin {
    @Redirect(method="scheduledTick",at=@At(value="INVOKE",target="Lnet/minecraft/block/FallingBlock;canFallThrough(Lnet/minecraft/block/BlockState;)Z"))
    private boolean goldcraft$hostBelow(BlockState below,BlockState state,ServerWorld world,BlockPos pos,Random random){
        return FallingBlock.canFallThrough(below)&&!HostBlockSupport.falling(world,pos);
    }
}
