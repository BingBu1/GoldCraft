package dev.goldcraft.client.mixin;

import dev.goldcraft.client.EntityExporter;
import net.minecraft.client.texture.AbstractTexture;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.PlayerSkinTexture;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(PlayerSkinTexture.class)
abstract class SkinTextureUploadMixin {
    @Inject(method="uploadTexture",at=@At("TAIL"))
    private void goldcraft$skinChanged(NativeImage image,CallbackInfo ci){EntityExporter.textureChanged((AbstractTexture)(Object)this);}
}
