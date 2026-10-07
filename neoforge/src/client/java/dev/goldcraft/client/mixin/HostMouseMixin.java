package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostKeys;
import net.minecraft.client.Mouse;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Mouse.class)
abstract class HostMouseMixin {
    @Inject(method="lockCursor",at=@At("HEAD"),cancellable=true)
    private void goldcraft$keepHostCapture(CallbackInfo ci){if(HostKeys.dispatching())ci.cancel();}
}
