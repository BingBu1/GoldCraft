package dev.goldcraft.client.mixin;
import net.minecraft.client.render.RenderPhase;
import net.minecraft.util.Identifier;
import java.util.Optional;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;
@Mixin(RenderPhase.TextureBase.class)
public interface RenderTextureAccessor {
    @Invoker("getId") Optional<Identifier> goldcraft$id();
}
