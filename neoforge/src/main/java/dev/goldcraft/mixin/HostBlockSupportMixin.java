package dev.goldcraft.mixin;

import dev.goldcraft.world.HostBlockSupport;
import net.minecraft.block.AbstractBlock;
import net.minecraft.block.BlockState;
import net.minecraft.block.SideShapeType;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.BlockView;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(AbstractBlock.AbstractBlockState.class)
public abstract class HostBlockSupportMixin {
    @Inject(method="isSideSolid",at=@At("RETURN"),cancellable=true)
    private void goldcraft$hostSupport(BlockView world,BlockPos pos,Direction face,SideShapeType type,
                                      CallbackInfoReturnable<Boolean> result){
        if(!result.getReturnValue()&&HostBlockSupport.side((BlockState)(Object)this,world,pos,face,type))
            result.setReturnValue(true);
    }
}
