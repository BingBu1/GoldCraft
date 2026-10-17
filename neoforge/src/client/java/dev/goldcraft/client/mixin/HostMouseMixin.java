package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostKeys;
import dev.goldcraft.client.HostFocus;
import dev.goldcraft.client.HostUi;
import net.minecraft.client.Mouse;
import net.minecraft.client.util.GlfwUtil;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Mouse.class)
abstract class HostMouseMixin {
    @Shadow private double cursorDeltaX;
    @Shadow private double cursorDeltaY;
    @Shadow private double lastTickTime;

    @Inject(method="lockCursor",at=@At("HEAD"),cancellable=true)
    private void goldcraft$keepHostCapture(CallbackInfo ci){
        if(HostKeys.dispatching()||HostUi.dispatching()||HostFocus.remote())ci.cancel();
    }

    @Inject(method="isCursorLocked",at=@At("HEAD"),cancellable=true)
    private void goldcraft$hostCapture(CallbackInfoReturnable<Boolean> ci){
        if(HostFocus.remote())ci.setReturnValue(HostFocus.captured());
    }

    // Logical capture must not consume this background window's real pointer
    // or turn the player a second time. CS supplies gameplay look and GUI motion.
    @Inject(method="onCursorPos",at=@At("HEAD"),cancellable=true)
    private void goldcraft$hostPointer(long window,double x,double y,CallbackInfo ci){
        if(HostFocus.remote())ci.cancel();
    }

    @Inject(method="tick",at=@At("HEAD"),cancellable=true)
    private void goldcraft$hostMouseTick(CallbackInfo ci){
        if(HostFocus.remote()){
            cursorDeltaX=cursorDeltaY=0;lastTickTime=GlfwUtil.getTime();ci.cancel();
        }
    }
}
