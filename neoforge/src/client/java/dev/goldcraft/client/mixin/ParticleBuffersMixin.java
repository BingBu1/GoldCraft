package dev.goldcraft.client.mixin;

import dev.goldcraft.client.DirectMeshCapture;
import net.minecraft.client.render.BufferBuilderStorage;
import net.minecraft.client.render.VertexConsumerProvider;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Real layers execute setup/flush, including Epic Fight's ordered endLastBatch calls. */
@Mixin(BufferBuilderStorage.class)
abstract class ParticleBuffersMixin {
    @Inject(method="getEntityVertexConsumers",at=@At("HEAD"),cancellable=true)
    private void goldcraft$particleBuffers(CallbackInfoReturnable<VertexConsumerProvider.Immediate> ci){
        var provider=DirectMeshCapture.redirectedBuffers();if(provider!=null)ci.setReturnValue(provider);
    }
}
