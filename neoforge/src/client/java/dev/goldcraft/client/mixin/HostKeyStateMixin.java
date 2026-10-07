package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostKeys;
import net.minecraft.client.util.InputUtil;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(InputUtil.class)
abstract class HostKeyStateMixin {
    @Inject(method="isKeyPressed",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$held(long window,int code,CallbackInfoReturnable<Boolean> ci){
        if(HostKeys.overrides(window))ci.setReturnValue(HostKeys.keyDown(code));
    }
}
