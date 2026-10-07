package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HudExporter;
import net.minecraft.client.gui.hud.InGameHud;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(InGameHud.class)
public abstract class HudOverlayMixin {
    // Vignette multiplies the world colour; it cannot be baked into a transparent HUD image.
    @Inject(method="renderVignetteOverlay",at=@At("HEAD"),cancellable=true)
    private void goldcraft$skipVignette(CallbackInfo ci){if(HudExporter.rendering())ci.cancel();}
}
