package dev.goldcraft.client.mixin;

import dev.goldcraft.client.WorldExporter;
import dev.goldcraft.client.EntityExporter;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.SpriteContents;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(SpriteContents.class)
abstract class SpriteContentsMixin {
    @Inject(method="upload(IIII[Lnet/minecraft/client/texture/NativeImage;)V",at=@At("TAIL"))
    private void goldcraft$animatedPixels(int x,int y,int sourceX,int sourceY,NativeImage[] images,CallbackInfo ci) {
        SpriteContents self=(SpriteContents)(Object)this;
        WorldExporter.spriteUploaded(x,y,sourceX,sourceY,self.getWidth(),self.getHeight(),images[0]);
        EntityExporter.spriteUploaded();
    }
}
