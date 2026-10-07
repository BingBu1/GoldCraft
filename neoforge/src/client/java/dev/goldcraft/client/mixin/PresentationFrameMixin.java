package dev.goldcraft.client.mixin;

import dev.goldcraft.client.GoldCraftClient;
import dev.goldcraft.client.HostAudio;
import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(MinecraftClient.class)
public abstract class PresentationFrameMixin {
    @Inject(method="render",at=@At(value="INVOKE",target="Lnet/minecraft/client/sound/SoundManager;updateListenerPosition(Lnet/minecraft/client/render/Camera;)V"))
    private void goldcraft$audioFrame(boolean tick,CallbackInfo ci){HostAudio.frame((MinecraftClient)(Object)this);}

    // This point also runs when skipGameRender is set. Ticks alone only provide 20 animation frames/s.
    @Inject(method="render", at=@At(value="INVOKE", target="Lnet/minecraft/client/gl/Framebuffer;endWrite()V"))
    private void goldcraft$presentationFrame(boolean tick, CallbackInfo ci) {
        GoldCraftClient.presentationFrame((MinecraftClient)(Object)this);
    }
}
