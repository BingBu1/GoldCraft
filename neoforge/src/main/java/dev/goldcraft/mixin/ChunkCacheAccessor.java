package dev.goldcraft.mixin;

import net.minecraft.world.World;
import net.minecraft.world.chunk.ChunkCache;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(ChunkCache.class)
public interface ChunkCacheAccessor {
    @Accessor("world") World goldcraft$world();
}
