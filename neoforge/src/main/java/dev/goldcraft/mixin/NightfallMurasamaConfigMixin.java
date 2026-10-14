package dev.goldcraft.mixin;

import net.neoforged.fml.loading.FMLEnvironment;
import net.neoforged.neoforge.common.ModConfigSpec;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;

/** NightFall 3.4.0 reads a CLIENT option inside a server animation event. */
@Pseudo
@Mixin(targets="com.hm.efn.gameasset.animations.EFNMurasamaAnimations",remap=false)
public abstract class NightfallMurasamaConfigMixin {
    @Redirect(method="lambda$static$4",at=@At(value="INVOKE",
        target="Lnet/neoforged/neoforge/common/ModConfigSpec$BooleanValue;get()Ljava/lang/Object;"),require=1)
    private static Object goldcraft$serverVisualDefault(ModConfigSpec.BooleanValue value){
        // Its preceding VFXENABLE test uses a non-short-circuit OR. Preserve
        // sounds, damage and event execution; only avoid reading unloaded
        // CLIENT configuration on the dedicated side. Clients retain the option.
        return FMLEnvironment.dist.isDedicatedServer()?value.getDefault():value.get();
    }
}
