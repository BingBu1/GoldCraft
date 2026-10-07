package dev.goldcraft.mixin;

import dev.goldcraft.world.SharedVitals;
import net.minecraft.entity.LivingEntity;
import net.minecraft.server.network.ServerPlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(LivingEntity.class)
public abstract class SharedVitalsRemovalMixin {
    @Inject(method="updatePostDeath",at=@At("HEAD"),cancellable=true)
    private void goldcraft$awaitNativeDeath(CallbackInfo ci){
        if((Object)this instanceof ServerPlayerEntity player&&SharedVitals.deferRemoval(player))ci.cancel();
    }
}
