package dev.goldcraft.client.mixin;

import com.mojang.blaze3d.platform.GlStateManager;
import dev.goldcraft.client.DepthCopy;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value=GlStateManager.class,remap=false)
abstract class DepthBlitMixin {
    @Inject(method="_glBlitFrameBuffer",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$depth(int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1,int mask,int filter,CallbackInfo ci){
        if(DepthCopy.convert(sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1,mask,filter))ci.cancel();
    }
}
