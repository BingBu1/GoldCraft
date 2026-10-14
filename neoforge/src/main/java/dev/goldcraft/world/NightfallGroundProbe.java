package dev.goldcraft.world;

import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.block.PlantBlock;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;

/** Local adapter for NightFall's downward effect probe; never modifies blocks. */
public final class NightfallGroundProbe {
    private NightfallGroundProbe(){}
    public static BlockState read(World world,BlockPos position){
        // NightFall keeps subtracting from a float until it finds a block. An
        // empty host dimension has no bedrock; below the world this never ends.
        if(world.isOutOfHeightLimit(position))return Blocks.BEDROCK.getDefaultState();
        BlockState state=world.getBlockState(position);
        if((state.isAir()||state.getBlock() instanceof PlantBlock)
            &&!HostCollision.shape(world,position).isEmpty())return Blocks.STONE.getDefaultState();
        return state;
    }
}
