package dev.goldcraft.client.mixin;

import dev.goldcraft.client.WorldExporter;
import net.minecraft.block.BlockState;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.util.math.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ClientWorld.class)
public abstract class ClientWorldMixin {
    // Server block updates call World.setBlockState directly, bypassing the prediction override below.
    @Inject(method="handleBlockUpdate",at=@At("RETURN"))
    private void goldcraft$serverBlockChanged(BlockPos pos,BlockState state,int flags,CallbackInfo ci) {
        WorldExporter.blockChanged((ClientWorld)(Object)this,pos);
    }
    @Inject(method="setBlockState",at=@At("RETURN"))
    private void goldcraft$blockChanged(BlockPos pos,BlockState state,int flags,int maxUpdateDepth,CallbackInfoReturnable<Boolean> result) {
        if(result.getReturnValueZ()) WorldExporter.blockChanged((ClientWorld)(Object)this,pos);
    }
}
