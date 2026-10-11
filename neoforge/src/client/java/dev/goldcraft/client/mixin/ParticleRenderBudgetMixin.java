package dev.goldcraft.client.mixin;

import dev.goldcraft.client.ParticleExporter;
import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleManager;
import net.minecraft.client.render.Camera;
import net.minecraft.client.render.VertexConsumer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(ParticleManager.class)
abstract class ParticleRenderBudgetMixin {
    @Redirect(method="render",at=@At(value="INVOKE",target="Lnet/minecraft/client/particle/Particle;buildGeometry(Lnet/minecraft/client/render/VertexConsumer;Lnet/minecraft/client/render/Camera;F)V"))
    private void goldcraft$renderParticle(Particle particle,VertexConsumer consumer,Camera camera,float delta){
        if(ParticleExporter.allow(particle))particle.buildGeometry(consumer,camera,delta);
    }
}
