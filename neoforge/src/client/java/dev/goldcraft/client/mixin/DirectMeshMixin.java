package dev.goldcraft.client.mixin;

import dev.goldcraft.client.DirectMeshCapture;
import net.minecraft.client.render.BufferRenderer;
import net.minecraft.client.render.BuiltBuffer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(BufferRenderer.class)
abstract class DirectMeshMixin {
    @Inject(method="drawWithGlobalProgram",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$draw(BuiltBuffer buffer,CallbackInfo ci){if(DirectMeshCapture.capture(buffer,true))ci.cancel();}
    @Inject(method="draw",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$manual(BuiltBuffer buffer,CallbackInfo ci){if(DirectMeshCapture.capture(buffer,false))ci.cancel();}
}
