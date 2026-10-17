package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.goldcraft.world.NativeNutrition;
import net.minecraft.entity.player.HungerManager;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.world.GameRules;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(PlayerEntity.class)
abstract class NativeNutritionTickMixin {
    @WrapOperation(method="tick",at=@At(value="INVOKE",
        target="Lnet/minecraft/entity/player/HungerManager;update(Lnet/minecraft/entity/player/PlayerEntity;)V"))
    private void goldcraft$foodTick(HungerManager manager,PlayerEntity player,Operation<Void> original){
        if(NativeNutrition.vanillaTick(manager,player))original.call(manager,player);
    }
    @WrapOperation(method="tickMovement",at=@At(value="INVOKE",
        target="Lnet/minecraft/world/GameRules;getBoolean(Lnet/minecraft/world/GameRules$Key;)Z",ordinal=0))
    private boolean goldcraft$peacefulRecovery(GameRules rules,GameRules.Key<GameRules.BooleanRule> key,Operation<Boolean> original){
        return original.call(rules,key)&&!NativeNutrition.parked((PlayerEntity)(Object)this);
    }
}
