package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HudExporter;
import dev.goldcraft.client.HostUi;
import net.minecraft.client.gui.screen.Screen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Screen.class)
public abstract class HudScreenMixin {
    // Vanilla blur rebinds the MC main framebuffer. Keep the exported menu on its own target.
    @Inject(method="applyBlur",at=@At("HEAD"),cancellable=true)
    private void goldcraft$skipWorldBlur(CallbackInfo ci){if(HudExporter.rendering())ci.cancel();}
    @Inject(method="hasShiftDown",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$shift(CallbackInfoReturnable<Boolean> ci){if(HostUi.dispatching())ci.setReturnValue(HostUi.modifier(1));}
    @Inject(method="hasControlDown",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$control(CallbackInfoReturnable<Boolean> ci){if(HostUi.dispatching())ci.setReturnValue(HostUi.modifier(2));}
    @Inject(method="hasAltDown",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$alt(CallbackInfoReturnable<Boolean> ci){if(HostUi.dispatching())ci.setReturnValue(HostUi.modifier(4));}
}
