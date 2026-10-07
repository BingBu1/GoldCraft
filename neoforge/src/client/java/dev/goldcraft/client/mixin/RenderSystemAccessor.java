package dev.goldcraft.client.mixin;

import com.mojang.blaze3d.systems.RenderSystem;
import org.joml.Matrix4fStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Mutable;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(value=RenderSystem.class,remap=false)
public interface RenderSystemAccessor {
    @Mutable
    @Accessor("modelViewStack")
    static void goldcraft$setModelViewStack(Matrix4fStack stack) {throw new AssertionError();}
}
