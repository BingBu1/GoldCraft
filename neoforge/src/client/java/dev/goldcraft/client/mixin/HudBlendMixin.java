package dev.goldcraft.client.mixin;

import com.mojang.blaze3d.platform.GlStateManager;
import dev.goldcraft.client.HudExporter;
import org.lwjgl.opengl.GL11;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GlStateManager.class)
public abstract class HudBlendMixin {
    // Accumulate coverage as well as premultiplied colour, instead of punching holes in earlier GUI layers.
    @ModifyVariable(method="_blendFuncSeparate",at=@At("HEAD"),argsOnly=true,ordinal=2,remap=false)
    private static int goldcraft$sourceAlpha(int original){return HudExporter.rendering()?GL11.GL_ONE:original;}
    @ModifyVariable(method="_blendFuncSeparate",at=@At("HEAD"),argsOnly=true,ordinal=3,remap=false)
    private static int goldcraft$destinationAlpha(int original){return HudExporter.rendering()?GL11.GL_ONE_MINUS_SRC_ALPHA:original;}
    @Inject(method="_blendFunc",at=@At("HEAD"),cancellable=true,remap=false)
    private static void goldcraft$separateCoverage(int source,int destination,CallbackInfo ci){
        if(HudExporter.rendering()){GlStateManager._blendFuncSeparate(source,destination,GL11.GL_ONE,GL11.GL_ONE_MINUS_SRC_ALPHA);ci.cancel();}
    }
}
