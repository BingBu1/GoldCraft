package dev.goldcraft.mixin;

import dev.goldcraft.world.HostBlockSupport;
import net.minecraft.block.BlockState;
import net.minecraft.block.FallingBlock;
import net.minecraft.entity.FallingBlockEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(FallingBlockEntity.class)
public abstract class HostFallingEntityMixin {
    @Redirect(method="tick",at=@At(value="INVOKE",target="Lnet/minecraft/block/FallingBlock;canFallThrough(Lnet/minecraft/block/BlockState;)Z"))
    private boolean goldcraft$hostLanding(BlockState below){
        var entity=(FallingBlockEntity)(Object)this;
        return FallingBlock.canFallThrough(below)&&!HostBlockSupport.falling(entity.getWorld(),entity.getBlockPos());
    }
}
