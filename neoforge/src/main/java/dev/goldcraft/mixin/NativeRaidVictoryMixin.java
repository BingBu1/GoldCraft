package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.goldcraft.world.NativeRaids;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.village.raid.Raid;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(Raid.class)
abstract class NativeRaidVictoryMixin {
    @WrapOperation(method="tick",at=@At(value="INVOKE",
        target="Lnet/minecraft/entity/LivingEntity;addStatusEffect(Lnet/minecraft/entity/effect/StatusEffectInstance;)Z"))
    private boolean goldcraft$nativeVictory(LivingEntity hero,StatusEffectInstance effect,Operation<Boolean> original){
        boolean applied=original.call(hero,effect);
        NativeRaids.victory(hero);
        return applied;
    }
}
