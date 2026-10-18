package dev.goldcraft.mixin;

import dev.goldcraft.world.NativeRaids;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffects;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(targets={"net.minecraft.entity.effect.BadOmenStatusEffect","net.minecraft.entity.effect.RaidOmenStatusEffect"})
abstract class NativeRaidOmenMixin {
    @Inject(method="applyUpdateEffect",at=@At("HEAD"),cancellable=true)
    private void goldcraft$nativeOmen(LivingEntity entity,int amplifier,CallbackInfoReturnable<Boolean> result){
        var player=NativeRaids.activePlayer(entity);
        if(player!=null)result.setReturnValue((Object)this==StatusEffects.BAD_OMEN.value()
            ?NativeRaids.badOmen(player,amplifier):NativeRaids.raidOmen(player));
    }
}
