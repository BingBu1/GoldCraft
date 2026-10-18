package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostedRenderTarget;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.Framebuffer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(MinecraftClient.class)
abstract class HostedFramebufferMixin {
    @Inject(method="getFramebuffer",at=@At("HEAD"),cancellable=true)
    private void goldcraft$renderTarget(CallbackInfoReturnable<Framebuffer> ci) {
        var target=HostedRenderTarget.current();
        if(target!=null)ci.setReturnValue(target);
    }
}
