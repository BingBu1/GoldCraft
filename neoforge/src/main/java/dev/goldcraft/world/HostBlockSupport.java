package dev.goldcraft.world;

import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.block.SideShapeType;
import net.minecraft.util.function.BooleanBiFunction;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.util.shape.VoxelShapes;
import net.minecraft.world.BlockView;

/** Host terrain contributes support without changing the chunk's block states. */
public final class HostBlockSupport {
    private static final VoxelShape CENTER = Block.createCuboidShape(7,0,7,9,10,9);
    private static final VoxelShape RIM = VoxelShapes.combineAndSimplify(VoxelShapes.fullCube(),
        Block.createCuboidShape(2,0,2,14,16,14),BooleanBiFunction.ONLY_FIRST);
    private HostBlockSupport(){}

    public static boolean side(BlockState state,BlockView world,BlockPos pos,Direction face,SideShapeType type){
        var host=HostCollision.shape(world,pos);
        if(host.isEmpty())return false;
        var shape=VoxelShapes.union(state.getSidesShape(world,pos),host);
        // Match the pinned vanilla FULL/CENTER/RIGID tests, including partial
        // surfaces. Block-state shape caches cannot cache position-dependent BSP.
        return switch(type){
            case FULL -> Block.isFaceFullSquare(shape,face);
            case CENTER -> !VoxelShapes.matchesAnywhere(shape.getFace(face),CENTER,BooleanBiFunction.ONLY_SECOND);
            case RIGID -> !VoxelShapes.matchesAnywhere(shape.getFace(face),RIM,BooleanBiFunction.ONLY_SECOND);
        };
    }

    public static boolean falling(BlockView world,BlockPos placed){
        // Vanilla falling blocks also rest above partial solid blocks (slabs,
        // fences). Actual falling motion still uses the host collision shapes.
        return !HostCollision.shape(world,placed.down()).isEmpty();
    }
}
