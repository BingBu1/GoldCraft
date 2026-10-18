package dev.goldcraft.mixin;

import dev.goldcraft.world.NativeHealth;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.attribute.EntityAttribute;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.registry.entry.RegistryEntry;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LivingEntity.class)
abstract class NativeHealthMixin {
    @Inject(method="getAttributeValue",at=@At("HEAD"))
    private void goldcraft$clientHealthRange(RegistryEntry<EntityAttribute> attribute,CallbackInfoReturnable<Double> cir){
        if(attribute.matches(EntityAttributes.GENERIC_MAX_HEALTH))NativeHealth.observeClientAttribute((LivingEntity)(Object)this);
    }
    @Inject(method="getAttributeValue",at=@At("RETURN"),cancellable=true)
    private void goldcraft$logicalHealthRange(RegistryEntry<EntityAttribute> attribute,CallbackInfoReturnable<Double> cir){
        if(attribute.matches(EntityAttributes.GENERIC_MAX_HEALTH))cir.setReturnValue(NativeHealth.logicalMaximum((LivingEntity)(Object)this,cir.getReturnValue()));
    }
    @Inject(method="updateAttribute",at=@At("HEAD"),cancellable=true)
    private void goldcraft$nativeCapOwnsHealth(RegistryEntry<EntityAttribute> attribute,CallbackInfo ci){
        // The native consumer applies cap decreases without an injury. Applying
        // this vanilla setHealth too would become a second VITALS_DELTA injury.
        if(attribute.matches(EntityAttributes.GENERIC_MAX_HEALTH)&&NativeHealth.owned((LivingEntity)(Object)this))ci.cancel();
    }
    @ModifyArg(method="setHealth",at=@At(value="INVOKE",target="Lnet/minecraft/util/math/MathHelper;clamp(FFF)F"),index=2)
    private float goldcraft$preserveNativeReward(float maximum){return NativeHealth.storageMaximum((LivingEntity)(Object)this,maximum);}
}
