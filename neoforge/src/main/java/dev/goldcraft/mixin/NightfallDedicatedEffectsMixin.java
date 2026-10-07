package dev.goldcraft.mixin;

import net.neoforged.fml.loading.FMLEnvironment;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** NightFall 3.4.0 calls its CLIENT-only visual setting during server travel events. */
@Pseudo
@Mixin(targets = "com.hm.efn.util.EffekUnits", remap = false)
public abstract class NightfallDedicatedEffectsMixin {
    @Inject(method = "VFXENABLE()Z", at = @At("HEAD"), cancellable = true)
    private static void goldcraft$dedicatedHasNoClientEffects(CallbackInfoReturnable<Boolean> result) {
        // Verified against the installed method: it reads EFNClientConfig.VFX_PLUS
        // before testing the optional AAA Particles mod. CLIENT config never loads
        // on a dedicated server. Client visuals still execute the original method.
        if (FMLEnvironment.dist.isDedicatedServer()) result.setReturnValue(false);
    }
}
