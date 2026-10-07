package dev.goldcraft.mixin;

import dev.goldcraft.world.HostWorldReset;
import net.minecraft.block.BlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import net.minecraft.world.chunk.WorldChunk;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(WorldChunk.class)
public abstract class WorldChunkResetMixin {
    @Inject(method="setBlockState",at=@At("RETURN"))
    private void goldcraft$objectGeometryChanged(BlockPos pos,BlockState state,boolean moved,CallbackInfoReturnable<BlockState> cir){
        if(cir.getReturnValue()!=null)dev.goldcraft.world.MinecraftObjects.changed((WorldChunk)(Object)this);
    }
    @Redirect(method="setBlockState",at=@At(value="INVOKE",target="Lnet/minecraft/block/BlockState;onStateReplaced(Lnet/minecraft/world/World;Lnet/minecraft/util/math/BlockPos;Lnet/minecraft/block/BlockState;Z)V"))
    private void goldcraft$resetWithoutDrops(BlockState old,World world,BlockPos pos,BlockState next,boolean moved) {
        if(!HostWorldReset.clearing())old.onStateReplaced(world,pos,next,moved);
    }
}
