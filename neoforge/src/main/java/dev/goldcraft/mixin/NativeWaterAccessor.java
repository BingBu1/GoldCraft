package dev.goldcraft.mixin;

import net.minecraft.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(LivingEntity.class)
public interface NativeWaterAccessor {
    @Invoker("getBaseMovementSpeedMultiplier") float goldcraft$waterDrag();
}
