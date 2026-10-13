package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.goldcraft.world.NativeStatusExecution;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffect;
import net.minecraft.entity.projectile.thrown.PotionEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(PotionEntity.class)
abstract class NativeStatusInstantMixin {
    @WrapOperation(method="applySplashPotion",at=@At(value="INVOKE",
        target="Lnet/minecraft/entity/effect/StatusEffect;applyInstantEffect(Lnet/minecraft/entity/Entity;Lnet/minecraft/entity/Entity;Lnet/minecraft/entity/LivingEntity;ID)V"),require=1)
    private void goldcraft$instantEffect(StatusEffect effect,Entity direct,Entity attacker,LivingEntity target,int amplifier,double proximity,Operation<Void> original){
        try(var scope=NativeStatusExecution.instant(target,direct,attacker)){original.call(effect,direct,attacker,target,amplifier,proximity);}
    }
}
