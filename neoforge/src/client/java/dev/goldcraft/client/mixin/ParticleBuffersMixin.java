package dev.goldcraft.client.mixin;

import dev.goldcraft.client.ParticleExporter;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumer;
import net.minecraft.client.render.VertexConsumerProvider;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(VertexConsumerProvider.Immediate.class)
public abstract class ParticleBuffersMixin {
    // Vanilla item-pickup and elder-guardian particles use the entity buffers instead of their supplied consumer.
    @Inject(method="getBuffer",at=@At("HEAD"),cancellable=true)
    private void goldcraft$particleGeometry(RenderLayer layer,CallbackInfoReturnable<VertexConsumer> ci) {
        VertexConsumer capture=ParticleExporter.captureBuffer(layer);
        if(capture!=null)ci.setReturnValue(capture);
    }
    @Inject(method="draw()V",at=@At("HEAD"),cancellable=true)
    private void goldcraft$noParticleFlush(CallbackInfo ci){if(ParticleExporter.capturing())ci.cancel();}
}
