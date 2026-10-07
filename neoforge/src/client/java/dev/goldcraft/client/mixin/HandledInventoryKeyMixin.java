package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostUi;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.ingame.HandledScreen;
import net.minecraft.client.option.KeyBinding;
import net.minecraft.client.util.InputUtil;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(HandledScreen.class)
abstract class HandledInventoryKeyMixin {
    // NeoForge replaces vanilla matchesKey with its context-aware
    // extension. Text fields still get first refusal before this comparison.
    @Redirect(method="keyPressed",at=@At(value="INVOKE",target="Lnet/minecraft/client/option/KeyBinding;isActiveAndMatches(Lnet/minecraft/client/util/InputUtil$Key;)Z"))
    private boolean goldcraft$inventoryToggle(KeyBinding binding,InputUtil.Key key){
        if(HostUi.dispatching()&&binding==MinecraftClient.getInstance().options.inventoryKey)
            return key.equals(InputUtil.fromKeyCode(GLFW.GLFW_KEY_I,0));
        return binding.isActiveAndMatches(key);
    }
}
