package dev.goldcraft.mixin;

import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.HostNavigation;
import net.minecraft.entity.ai.pathing.EntityNavigation;
import net.minecraft.entity.ai.pathing.LandPathNodeMaker;
import net.minecraft.entity.ai.pathing.Path;
import net.minecraft.entity.ai.pathing.PathNodeMaker;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(EntityNavigation.class)
public abstract class EntityNavigationMixin {
    @Shadow @Final protected World world;
    @Shadow protected PathNodeMaker nodeMaker;
    @Shadow protected Path currentPath;
    @Shadow public abstract void recalculatePath();
    @Unique private long goldcraft$geometryRevision;

    @Inject(method="isValidPosition",at=@At("RETURN"),cancellable=true)
    private void goldcraft$randomTarget(BlockPos pos,CallbackInfoReturnable<Boolean> result){
        if(!result.getReturnValue()&&nodeMaker instanceof LandPathNodeMaker&&Double.isFinite(HostNavigation.supportY(world,pos)))result.setReturnValue(true);
    }

    @Inject(method="adjustTargetY",at=@At("RETURN"),cancellable=true)
    private void goldcraft$moveTarget(Vec3d target,CallbackInfoReturnable<Double> result){
        BlockPos pos=BlockPos.ofFloored(target);
        if(nodeMaker instanceof LandPathNodeMaker&&Double.isFinite(HostNavigation.supportY(world,pos)))result.setReturnValue(LandPathNodeMaker.getFeetY(world,pos));
    }

    @Inject(method="tick",at=@At("HEAD"))
    private void goldcraft$movingObstacles(CallbackInfo ci){
        long revision=HostCollision.revision(world);
        if(goldcraft$geometryRevision!=revision){
            goldcraft$geometryRevision=revision;
            // A closed door can leave a finished partial route. Its destination
            // still matters when the brush opens; cancelled navigation has null
            // currentPath, while a successfully completed route reachesTarget.
            if(revision!=0&&nodeMaker instanceof LandPathNodeMaker&&currentPath!=null
                &&(!currentPath.isFinished()||!currentPath.reachesTarget()))recalculatePath();
        }
    }
}
