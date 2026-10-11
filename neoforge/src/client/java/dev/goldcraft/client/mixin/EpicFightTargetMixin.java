package dev.goldcraft.client.mixin;

import dev.goldcraft.client.HostEpicTargets;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.player.PlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Exact Epic Fight 21.17.3.1 native-body targeting compatibility. */
@Pseudo
@Mixin(targets="yesman.epicfight.api.client.camera.EpicFightCameraAPI",remap=false)
abstract class EpicFightTargetMixin {
    @Inject(method="predicateFocusableEntity",at=@At("RETURN"),cancellable=true)
    private void goldcraft$nativeFaction(Entity target,CallbackInfoReturnable<Boolean> result) {
        if(target instanceof NativePlayerHitboxEntity nativeBody)
            result.setReturnValue(result.getReturnValueZ()&&HostEpicTargets.canTarget(nativeBody));
    }
    @Redirect(method="postClientTick",at=@At(value="INVOKE",target="Lnet/minecraft/world/entity/LivingEntity;isInvisibleTo(Lnet/minecraft/world/entity/player/Player;)Z"))
    private boolean goldcraft$nativeVisibility(LivingEntity target,PlayerEntity viewer) {
        // EmptyEntityRenderer hides this proxy; its invisibility flag must not
        // erase a visible GoldSrc enemy from Epic's current-target retention.
        return target instanceof NativePlayerHitboxEntity nativeBody
            ?!HostEpicTargets.canTarget(nativeBody):target.isInvisibleTo(viewer);
    }
}
