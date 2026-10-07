package dev.goldcraft.client.mixin;
import it.unimi.dsi.fastutil.longs.Long2ObjectMap;
import java.util.SortedSet;
import net.minecraft.client.render.WorldRenderer;
import net.minecraft.entity.player.BlockBreakingInfo;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(WorldRenderer.class)
public interface WorldRendererAccessor {
    @Accessor("blockBreakingProgressions") Long2ObjectMap<SortedSet<BlockBreakingInfo>> goldcraft$breakingProgress();
}
