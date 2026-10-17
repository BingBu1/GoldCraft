package dev.goldcraft.client.mixin;

import dev.goldcraft.client.DepthCopy;
import org.lwjgl.opengl.GL41C;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/** AAA's raw LWJGL copies bypass Minecraft's common depth-blit entry. */
@Pseudo
@Mixin(targets="mod.chloeprime.aaaparticles.client.render.RenderUtil",remap=false)
abstract class AaaDepthBlitMixin {
    @Redirect(method={"copyDepthSafely*","copySafely"},at=@At(value="INVOKE",target="Lorg/lwjgl/opengl/GL41C;glBlitFramebuffer(IIIIIIIIII)V"),require=3)
    private static void goldcraft$depth(int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1,int mask,int filter){
        if(!DepthCopy.convert(sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1,mask,filter))
            GL41C.glBlitFramebuffer(sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1,mask,filter);
    }
}
