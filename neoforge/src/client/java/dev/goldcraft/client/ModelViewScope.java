package dev.goldcraft.client;

import com.mojang.blaze3d.systems.RenderSystem;
import dev.goldcraft.client.mixin.RenderSystemAccessor;
import java.util.function.Consumer;
import org.joml.Matrix4fStack;

/** Offscreen Mod rendering must not leak pushes or overwrite any caller stack entries. */
final class ModelViewScope implements AutoCloseable {
    private final Matrix4fStack previous;
    private final Consumer<Matrix4fStack> replace;

    static ModelViewScope begin(Matrix4fStack scratch) {
        RenderSystem.assertOnRenderThread();
        return new ModelViewScope(RenderSystem.getModelViewStack(),scratch,RenderSystemAccessor::goldcraft$setModelViewStack);
    }

    ModelViewScope(Matrix4fStack previous,Matrix4fStack scratch,Consumer<Matrix4fStack> replace) {
        if(previous==scratch)throw new IllegalArgumentException("Model-view scratch stack is already active");
        this.previous=previous;
        this.replace=replace;
        scratch.clear();
        scratch.set(previous);
        replace.accept(scratch);
    }

    @Override public void close() {replace.accept(previous);}
}
