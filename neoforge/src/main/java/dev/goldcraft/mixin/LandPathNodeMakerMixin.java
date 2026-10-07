package dev.goldcraft.mixin;

import dev.goldcraft.world.HostNavigation;
import net.minecraft.block.BlockState;
import net.minecraft.entity.ai.pathing.LandPathNodeMaker;
import net.minecraft.entity.ai.pathing.PathContext;
import net.minecraft.entity.ai.pathing.PathNodeMaker;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.MathHelper;
import net.minecraft.world.BlockView;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LandPathNodeMaker.class)
public abstract class LandPathNodeMakerMixin extends PathNodeMaker {
    @Inject(method="getFeetY(Lnet/minecraft/world/BlockView;Lnet/minecraft/util/math/BlockPos;)D",at=@At("RETURN"),cancellable=true)
    private static void goldcraft$feet(BlockView view,BlockPos pos,CallbackInfoReturnable<Double> result){
        double hostY=HostNavigation.supportY(view,pos);
        if(Double.isFinite(hostY))result.setReturnValue(Math.max(hostY,result.getReturnValue()));
    }

    @Redirect(method="getStart()Lnet/minecraft/entity/ai/pathing/PathNode;",at=@At(value="INVOKE",target="Lnet/minecraft/util/math/MathHelper;floor(D)I"))
    private int goldcraft$fractionalStart(double y){return HostNavigation.startY(entity,MathHelper.floor(y));}

    @Redirect(method="getStart()Lnet/minecraft/entity/ai/pathing/PathNode;",at=@At(value="INVOKE",target="Lnet/minecraft/entity/ai/pathing/PathContext;getBlockState(Lnet/minecraft/util/math/BlockPos;)Lnet/minecraft/block/BlockState;"))
    private BlockState goldcraft$startSupport(PathContext context,BlockPos pos){
        return HostNavigation.scanState(context.getWorld(),pos,context.getBlockState(pos));
    }
}
