package dev.goldcraft.world;

import dev.goldcraft.bridge.MapMining;
import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;

/** Gameplay equivalents for native categories. This does not place MC blocks or change BSP textures. */
public final class HostMiningMaterials {
    private HostMiningMaterials() {}

    public static BlockState state(int material) {
        return switch (material) {
            case MapMining.STONE -> Blocks.STONE.getDefaultState();
            case MapMining.WOOD -> Blocks.OAK_PLANKS.getDefaultState();
            case MapMining.METAL -> Blocks.IRON_BLOCK.getDefaultState();
            case MapMining.GLASS -> Blocks.GLASS.getDefaultState();
            case MapMining.SOIL -> Blocks.DIRT.getDefaultState();
            case MapMining.TILE -> Blocks.TERRACOTTA.getDefaultState();
            case MapMining.FLESH -> Blocks.MUSHROOM_STEM.getDefaultState();
            case MapMining.UNBREAKABLE -> Blocks.BEDROCK.getDefaultState();
            case MapMining.WATER -> Blocks.WATER.getDefaultState();
            case MapMining.SNOW -> Blocks.SNOW_BLOCK.getDefaultState();
            default -> throw new IllegalArgumentException("Unknown host material");
        };
    }
}
