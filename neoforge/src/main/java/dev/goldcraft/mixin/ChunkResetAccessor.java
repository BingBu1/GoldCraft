package dev.goldcraft.mixin;

import net.minecraft.nbt.NbtCompound;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.chunk.Chunk;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
import java.util.Map;

@Mixin(Chunk.class)
public interface ChunkResetAccessor {
    @Accessor("blockEntityNbts") Map<BlockPos,NbtCompound> goldcraft$pendingBlockEntities();
}
