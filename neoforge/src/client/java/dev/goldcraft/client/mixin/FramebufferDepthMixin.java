package dev.goldcraft.client.mixin;

import dev.goldcraft.client.DepthCopy;
import net.minecraft.client.gl.Framebuffer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Framebuffer.class)
abstract class FramebufferDepthMixin {
    @Inject(method="initFbo",at=@At("TAIL"))
    private void goldcraft$allocated(int width,int height,boolean getError,CallbackInfo ci){
        Framebuffer target=(Framebuffer)(Object)this;
        if(target.useDepthAttachment)DepthCopy.allocated(target.fbo,target.getDepthAttachment(),target.textureWidth,target.textureHeight);
    }
    @Inject(method="delete",at=@At("HEAD"))
    private void goldcraft$deleted(CallbackInfo ci){DepthCopy.deleted(((Framebuffer)(Object)this).fbo);}
}
