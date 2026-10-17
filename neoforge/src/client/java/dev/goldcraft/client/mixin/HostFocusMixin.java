package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostFocus;
import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(MinecraftClient.class)
abstract class HostFocusMixin {
    @Inject(method="isWindowFocused",at=@At("HEAD"),cancellable=true)
    private void goldcraft$hostFocus(CallbackInfoReturnable<Boolean> ci){
        if(HostFocus.remote())ci.setReturnValue(HostFocus.focused());
    }
}
