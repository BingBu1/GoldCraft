package dev.goldcraft.mixin;

import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/** The real equipment-wear/armor stage, including NeoForge's armor-hurt event. */
@Mixin(LivingEntity.class)
public interface NativeArmorAccessor {
    @Invoker("applyArmorToDamage")
    float goldcraft$applyNativeArmor(DamageSource source,float amount);
}
