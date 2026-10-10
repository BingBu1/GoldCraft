package dev.goldcraft.client.mixin;

import dev.goldcraft.client.RenderLayerDelegate;
import net.minecraft.client.render.RenderLayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** YSM 2.6.5's entity_translucent_ysm wrapper delegates its drawing state. */
@Pseudo
@Mixin(targets="com.elfmcys.yesstevemodel.O0oo0OOo0oOo00OO00oOO0oO",remap=false)
abstract class YsmRenderLayerMixin implements RenderLayerDelegate {
    @Unique private RenderLayer goldcraft$delegate;

    @Inject(method="<init>(Lnet/minecraft/client/renderer/RenderType;)V",at=@At("RETURN"))
    private void goldcraft$captureDelegate(RenderLayer delegate,CallbackInfo ci) {
        goldcraft$delegate=delegate;
    }

    @Override public RenderLayer goldcraft$delegateLayer() {
        return goldcraft$delegate;
    }
}
