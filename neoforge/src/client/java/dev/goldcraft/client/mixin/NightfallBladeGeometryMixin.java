package dev.goldcraft.client.mixin;

import dev.goldcraft.client.VixPostCapture;
import com.llamalad7.mixinextras.sugar.Local;
import org.joml.Vector3f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Exact NightFall3.4.0 JCBladeTrail: reject an undefined whole particle before the first vertex. */
@Pseudo
@Mixin(targets="com.hm.efn.client.particle.DMC.JCBladeTrail",remap=false)
abstract class NightfallBladeGeometryMixin {
    @Inject(method="render",at=@At(value="INVOKE",target="Lorg/joml/Vector3f;mul(F)Lorg/joml/Vector3f;",ordinal=1),cancellable=true)
    private void goldcraft$definedBlade(CallbackInfo callback,
        @Local(index=10) Vector3f width,@Local(index=12) float fade) {
        if(VixPostCapture.rejectUndefinedNightfallBlade(width,fade))callback.cancel();
    }
}
