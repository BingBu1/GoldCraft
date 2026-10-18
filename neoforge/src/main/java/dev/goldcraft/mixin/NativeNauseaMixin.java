package dev.goldcraft.mixin;

import dev.goldcraft.world.NativeNausea;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffect;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.registry.entry.RegistryEntry;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LivingEntity.class)
abstract class NativeNauseaMixin {
    @Inject(method="removeStatusEffectInternal",at=@At("RETURN"))
    private void goldcraft$nauseaRemovedInternal(RegistryEntry<StatusEffect> type,CallbackInfoReturnable<StatusEffectInstance> cir){
        if(cir.getReturnValue()!=null)NativeNausea.removed((LivingEntity)(Object)this,cir.getReturnValue());
    }
    @Inject(method="onStatusEffectRemoved",at=@At("HEAD"))
    private void goldcraft$nauseaRemoved(StatusEffectInstance effect,CallbackInfo ci){
        NativeNausea.removed((LivingEntity)(Object)this,effect);
    }
}
