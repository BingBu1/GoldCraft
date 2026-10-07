package dev.goldcraft.client.mixin;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.RenderPhase;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(RenderLayer.MultiPhaseParameters.class)
public interface RenderParametersAccessor {
    @Accessor("texture") RenderPhase.TextureBase goldcraft$texture();
    @Accessor("writeMaskState") RenderPhase.WriteMaskState goldcraft$writeMask();
}
