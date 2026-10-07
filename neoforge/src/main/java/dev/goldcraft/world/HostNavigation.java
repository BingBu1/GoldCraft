package dev.goldcraft.world;

import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.entity.mob.MobEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.MathHelper;
import net.minecraft.world.BlockView;

/** The vanilla graph stays in integer cells; physical support retains its BSP height. */
public final class HostNavigation {
    private HostNavigation(){}

    // Used only by vanilla navigation's air/solid scans, never stored in a chunk or cache.
    public static BlockState scanState(BlockView view,BlockPos pos,BlockState vanilla){
        return vanilla.isAir()&&!HostCollision.shape(view,pos).isEmpty()?Blocks.STONE.getDefaultState():vanilla;
    }

    public static double supportY(BlockView view,BlockPos node){
        var shape=HostCollision.shape(view,node.down());
        return shape.isEmpty()?Double.NaN:node.getY()-1+shape.getMax(Direction.Axis.Y);
    }

    public static int startY(MobEntity entity,int vanilla){
        if(!entity.isOnGround()||entity.isInFluid())return vanilla;
        var below=BlockPos.ofFloored(entity.getX(),entity.getY()-0.001,entity.getZ());
        return HostCollision.shape(entity.getWorld(),below).isEmpty()?vanilla:MathHelper.ceil(entity.getY()-0.001);
    }
}
