package dev.goldcraft.client.mixin;

import dev.goldcraft.client.VixPostCapture;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Coerce;
import org.spongepowered.asm.mixin.injection.Redirect;

/** Optional, exact VIX 1.0.9 API boundary; no target allocation during hosted CPU capture. */
@Pseudo
@Mixin(targets="com.guhao.vix.client.pipeline.PostParticleRenderType",remap=false)
abstract class VixPostParticleMixin {
    @Redirect(method="beginPost",at=@At(value="INVOKE",target="Lcom/guhao/vix/client/pipeline/PostEffectPipelines$Pipeline;start()V"))
    private void goldcraft$start(@Coerce Object pipeline){VixPostCapture.start(pipeline);}
    @Redirect(method="finish",at=@At(value="INVOKE",target="Lcom/guhao/vix/client/pipeline/PostEffectPipelines$Pipeline;suspend()V"))
    private void goldcraft$suspend(@Coerce Object pipeline){VixPostCapture.suspend(pipeline);}
}
