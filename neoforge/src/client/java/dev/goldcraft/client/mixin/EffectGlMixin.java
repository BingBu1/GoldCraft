package dev.goldcraft.client.mixin;

import com.mojang.blaze3d.platform.GlStateManager;
import dev.goldcraft.client.DirectMeshCapture;
import dev.goldcraft.client.EffectTextures;
import java.nio.IntBuffer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(value=GlStateManager.class,remap=false)
abstract class EffectGlMixin {
    @Inject(method="_activeTexture",at=@At("TAIL"))
    private static void goldcraft$unit(int unit,CallbackInfo ci){EffectTextures.active(unit);}
    @Inject(method="_bindTexture",at=@At("TAIL"))
    private static void goldcraft$bind(int texture,CallbackInfo ci){EffectTextures.bind(texture);}
    @Inject(method="_texImage2D",at=@At("TAIL"))
    private static void goldcraft$image(int target,int level,int internal,int width,int height,int border,int format,int type,IntBuffer pixels,CallbackInfo ci){EffectTextures.uploaded(target,level);}
    @Inject(method="_texSubImage2D",at=@At("TAIL"))
    private static void goldcraft$subImage(int target,int level,int x,int y,int width,int height,int format,int type,long pixels,CallbackInfo ci){EffectTextures.uploaded(target,level);}
    @Inject(method="_deleteTexture",at=@At("TAIL"))
    private static void goldcraft$delete(int texture,CallbackInfo ci){EffectTextures.deleted(texture);}
    @Inject(method="_deleteTextures",at=@At("TAIL"))
    private static void goldcraft$deletes(int[] textures,CallbackInfo ci){for(int texture:textures)EffectTextures.deleted(texture);}
    @Inject(method="_blendEquation",at=@At("TAIL"))
    private static void goldcraft$equation(int value,CallbackInfo ci){DirectMeshCapture.blendEquation(value);}
    @Inject(method="_blendFunc",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$blend(int source,int destination,CallbackInfo ci){
        // glBlendFunc affects alpha as well as RGB; vanilla only caches its RGB changes.
        if(DirectMeshCapture.capturing()){GlStateManager._blendFuncSeparate(source,destination,source,destination);ci.cancel();}
    }
}
