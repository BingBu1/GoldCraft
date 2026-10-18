package dev.goldcraft.client.mixin;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(targets="net.minecraft.client.render.RenderPhase$Toggleable")
public interface RenderToggleAccessor {
    @Accessor("enabled") boolean goldcraft$enabled();
}
