package dev.goldcraft.client.mixin;

import dev.goldcraft.client.GoldCraftClient;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.GameRenderer;
import net.minecraft.client.render.RenderTickCounter;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GameRenderer.class)
public abstract class HostPresentationMixin {
    @Shadow @Final private MinecraftClient client;
    @Shadow private float viewDistance;

    @Inject(method="render",at=@At("HEAD"),cancellable=true)
    private void goldcraft$hostPresentation(RenderTickCounter ticks,boolean tick,CallbackInfo ci){
        if(!GoldCraftClient.presentationOnly(client))return;
        // The host draws the world. Simulation, texture ticks and the separate
        // hand/HUD/particle export keep running; focus or an overlay restores vanilla rendering.
        if(client.getCameraEntity()==null)client.setCameraEntity(client.player);
        viewDistance=client.options.getClampedViewDistance()*16;
        client.gameRenderer.updateCrosshairTarget(ticks.getTickDelta(false));
        GoldCraftClient.skippedStandaloneFrame();
        ci.cancel();
    }
}
