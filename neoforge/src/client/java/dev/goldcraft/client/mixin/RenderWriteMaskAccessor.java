package dev.goldcraft.client.mixin;

import net.minecraft.client.render.RenderPhase;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(RenderPhase.WriteMaskState.class)
public interface RenderWriteMaskAccessor {
    @Accessor("depth") boolean goldcraft$writesDepth();
}
