package dev.goldcraft.mixin;

import dev.goldcraft.world.SharedVitals;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.server.network.ServerPlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ServerPlayerEntity.class)
public abstract class SharedVitalsDeathMixin {
    @Inject(method="onDeath",at=@At("HEAD"),cancellable=true)
    private void goldcraft$confirmDeath(DamageSource source,CallbackInfo ci){
        // Vanilla armor, absorption and totem handling have already run. Defer
        // inventory drops/statistics until ReHLDS accepts the net health change.
        if(SharedVitals.deferDeath((ServerPlayerEntity)(Object)this,source))ci.cancel();
    }
}
