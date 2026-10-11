package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import dev.goldcraft.world.NativeSkillDamage;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.damage.DamageSource;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Coerce;

/** Preserve native authority and attribution in NightFall 3.4.0's end-cut helper. */
@Pseudo
@Mixin(targets="com.hm.efn.util.yamato.DMC_V_JC_Server",remap=false)
abstract class NightfallNativeSkillDamageMixin {
    @WrapOperation(method="HurtEntity",at=@At(value="INVOKE",
        target="Lnet/minecraft/world/entity/Entity;hurt(Lnet/minecraft/world/damagesource/DamageSource;F)Z"),require=1)
    private static boolean goldcraft$percentageDamage(Entity target,DamageSource source,float amount,Operation<Boolean> original,
            @Coerce Object patch,Entity selected,@Coerce Object animation,float multiplier,float ratio){
        if(target instanceof NativePlayerHitboxEntity&&!target.getWorld().isClient)
            source=NativeSkillDamage.attributed(source,patch);
        return original.call(target,source,amount);
    }
    @WrapOperation(method="HurtEntity",at=@At(value="INVOKE",
        target="Lnet/minecraft/world/entity/LivingEntity;setHealth(F)V"),require=2,expect=2,allow=2)
    private static void goldcraft$bossHealth(LivingEntity target,float health,Operation<Void> original,
            @Coerce Object patch,Entity selected,@Coerce Object animation,float multiplier,float ratio){
        if(target instanceof NativePlayerHitboxEntity&&!target.getWorld().isClient){
            float amount=target.getHealth()-health;
            if(amount>0)target.damage(NativeSkillDamage.attributed(target.getDamageSources().generic(),patch),amount);
        }else original.call(target,health);
    }
    @WrapOperation(method="HurtEntity",at=@At(value="INVOKE",
        target="Lnet/minecraft/world/entity/LivingEntity;die(Lnet/minecraft/world/damagesource/DamageSource;)V"),require=1)
    private static void goldcraft$nativeDeath(LivingEntity target,DamageSource source,Operation<Void> original){
        if(!(target instanceof NativePlayerHitboxEntity)||target.getWorld().isClient)original.call(target,source);
    }
}
