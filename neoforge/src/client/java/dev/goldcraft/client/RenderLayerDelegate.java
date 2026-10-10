package dev.goldcraft.client;

import net.minecraft.client.render.RenderLayer;

/** Material state retained by a verified wrapper around a vanilla render layer. */
public interface RenderLayerDelegate {
    RenderLayer goldcraft$delegateLayer();
}
