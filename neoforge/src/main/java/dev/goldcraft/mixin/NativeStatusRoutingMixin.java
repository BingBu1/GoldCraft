package dev.goldcraft.mixin;

import dev.goldcraft.world.*;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffect;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.registry.entry.RegistryEntry;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(LivingEntity.class)
abstract class NativeStatusRoutingMixin {
    @Inject(method="tickStatusEffects",at=@At("HEAD"),cancellable=true)
    private void goldcraft$awaitNativeHealth(CallbackInfo ci){
        if(NativeStatusEffects.waitingForHealth((LivingEntity)(Object)this))ci.cancel();
    }
    @Inject(method="addStatusEffect(Lnet/minecraft/entity/effect/StatusEffectInstance;Lnet/minecraft/entity/Entity;)Z",at=@At("HEAD"),cancellable=true)
    private void goldcraft$statusTarget(StatusEffectInstance effect,Entity source,CallbackInfoReturnable<Boolean> cir){
        var self=(LivingEntity)(Object)this;
        if(self.getWorld().isClient)return;
        ((NativeStatusInstance)(Object)effect).goldcraft$statusOrigin(NativeStatusOrigin.capture(source));
        var target=NativeStatusEffects.routed(self);
        if(target!=self)cir.setReturnValue(target.addStatusEffect(effect,source));
    }
    @Inject(method="removeStatusEffect",at=@At("HEAD"),cancellable=true)
    private void goldcraft$removeStatus(RegistryEntry<StatusEffect> type,CallbackInfoReturnable<Boolean> cir){
        var self=(LivingEntity)(Object)this;var target=NativeStatusEffects.routed(self);
        if(target!=self)cir.setReturnValue(target.removeStatusEffect(type));
    }
    @Inject(method="clearStatusEffects",at=@At("HEAD"),cancellable=true)
    private void goldcraft$clearStatus(CallbackInfoReturnable<Boolean> cir){
        var self=(LivingEntity)(Object)this;var target=NativeStatusEffects.routed(self);
        if(target!=self)cir.setReturnValue(target.clearStatusEffects());
    }
}
