package dev.goldcraft.mixin;

import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffectInstance;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(LivingEntity.class)
public interface NativeStatusLifecycleAccessor {
    @Invoker("onStatusEffectApplied") void goldcraft$statusAdded(StatusEffectInstance effect,Entity source);
    @Invoker("onStatusEffectRemoved") void goldcraft$statusRemoved(StatusEffectInstance effect);
}
