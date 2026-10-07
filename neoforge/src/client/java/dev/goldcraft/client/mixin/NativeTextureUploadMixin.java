package dev.goldcraft.client.mixin;

import dev.goldcraft.client.EntityExporter;
import net.minecraft.client.texture.AbstractTexture;
import net.minecraft.client.texture.NativeImageBackedTexture;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(NativeImageBackedTexture.class)
abstract class NativeTextureUploadMixin {
    @Inject(method="upload",at=@At("TAIL"))
    private void goldcraft$textureChanged(CallbackInfo ci){EntityExporter.textureChanged((AbstractTexture)(Object)this);}
}
