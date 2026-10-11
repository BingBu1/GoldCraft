package dev.goldcraft.client.mixin;

import dev.goldcraft.client.VixPostCapture;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Pseudo
@Mixin(targets="com.guhao.vix.client.pipeline.PostEffectPipelines",remap=false)
abstract class VixPostPipelinesMixin {
    @Inject(method="saveSharedDepth",at=@At("HEAD"),cancellable=true)
    private static void goldcraft$sharedDepth(CallbackInfo ci){if(VixPostCapture.skipSharedDepth())ci.cancel();}
}
