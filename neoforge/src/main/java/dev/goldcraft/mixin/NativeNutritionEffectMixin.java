package dev.goldcraft.mixin;

import dev.goldcraft.world.NativeNutrition;
import net.minecraft.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

@Mixin(targets={"net.minecraft.entity.effect.HungerStatusEffect","net.minecraft.entity.effect.SaturationStatusEffect"})
abstract class NativeNutritionEffectMixin {
    @ModifyVariable(method="applyUpdateEffect",at=@At("HEAD"),argsOnly=true,ordinal=0)
    private LivingEntity goldcraft$nutritionPlayer(LivingEntity entity){
        return NativeNutrition.playerTarget(entity);
    }
}
