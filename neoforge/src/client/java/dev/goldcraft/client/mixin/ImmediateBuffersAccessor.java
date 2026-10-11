package dev.goldcraft.client.mixin;

import java.util.SequencedMap;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumerProvider;
import net.minecraft.client.util.BufferAllocator;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(VertexConsumerProvider.Immediate.class)
public interface ImmediateBuffersAccessor {
    @Accessor("layerBuffers")
    SequencedMap<RenderLayer,BufferAllocator> goldcraft$fixedLayers();
}
