package dev.goldcraft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Coerce;

/** NightFall 3.4.0 declares each mesh optional, but dereferences absent offhand accessors. */
@Pseudo
@Mixin(targets="com.hm.efn.client.renderer.RenderMurasama",remap=false)
abstract class NightfallMurasamaMeshMixin {
    @WrapOperation(method="renderItemInHand",at=@At(value="INVOKE",
        target="Lyesman/epicfight/api/asset/AssetAccessor;get()Ljava/lang/Object;"),require=4,expect=4,allow=4)
    private Object goldcraft$optionalMesh(@Coerce Object accessor,Operation<Object> original) {
        // Continue through the renderer's existing null-mesh branch and balanced pose pop.
        // Configured accessors still execute exactly once, with their failures unchanged.
        return accessor==null?null:original.call(accessor);
    }
}
