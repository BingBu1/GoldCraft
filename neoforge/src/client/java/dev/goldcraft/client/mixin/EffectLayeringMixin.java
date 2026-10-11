package dev.goldcraft.client.mixin;

import dev.goldcraft.client.DirectMeshCapture;
import net.minecraft.client.render.RenderPhase;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Vanilla's view offset must use the CS camera, never absolute MC coordinates. */
@Mixin(RenderPhase.class)
abstract class EffectLayeringMixin {
    @Inject(method="startDrawing",at=@At("HEAD"),cancellable=true)
    private void goldcraft$start(CallbackInfo ci){
        if((Object)this==RenderPhase.VIEW_OFFSET_Z_LAYERING&&DirectMeshCapture.viewOffsetLayer(true))ci.cancel();
    }
    @Inject(method="endDrawing",at=@At("HEAD"),cancellable=true)
    private void goldcraft$end(CallbackInfo ci){
        if((Object)this==RenderPhase.VIEW_OFFSET_Z_LAYERING&&DirectMeshCapture.viewOffsetLayer(false))ci.cancel();
    }
}
