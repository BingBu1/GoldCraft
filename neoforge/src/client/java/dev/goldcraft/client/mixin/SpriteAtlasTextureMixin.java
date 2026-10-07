package dev.goldcraft.client.mixin;

import dev.goldcraft.client.WorldExporter;
import dev.goldcraft.client.ParticleExporter;
import dev.goldcraft.client.EntityExporter;
import net.minecraft.client.texture.SpriteAtlasTexture;
import net.minecraft.client.texture.SpriteLoader;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SpriteAtlasTexture.class)
abstract class SpriteAtlasTextureMixin {
    @Inject(method="upload",at=@At("TAIL"))
    private void goldcraft$atlasUploaded(SpriteLoader.StitchResult result,CallbackInfo ci) {
        WorldExporter.atlasUploaded((SpriteAtlasTexture)(Object)this);
        ParticleExporter.atlasUploaded((SpriteAtlasTexture)(Object)this);
        EntityExporter.textureChanged((SpriteAtlasTexture)(Object)this);
    }
    @Inject(method="tickAnimatedSprites",at=@At("HEAD"))
    private void goldcraft$beginAnimation(CallbackInfo ci) {
        WorldExporter.beginAtlasAnimation((SpriteAtlasTexture)(Object)this);
        EntityExporter.beginAtlasAnimation((SpriteAtlasTexture)(Object)this);
    }
    @Inject(method="tickAnimatedSprites",at=@At("TAIL"))
    private void goldcraft$endAnimation(CallbackInfo ci) {WorldExporter.endAtlasAnimation();EntityExporter.endAtlasAnimation();}
}
