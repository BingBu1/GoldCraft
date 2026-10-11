package dev.goldcraft.client;

import dev.goldcraft.client.mixin.RenderParametersAccessor;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.RenderPhase;

/** Preserve the actual phase identity, including layers supplied by other renderers. */
final class LayerSemantics {
    private LayerSemantics() {}

    static boolean viewOffset(RenderLayer.MultiPhaseParameters parameters) {
        return ((RenderParametersAccessor)(Object)parameters).goldcraft$layering()==RenderPhase.VIEW_OFFSET_Z_LAYERING;
    }
}
