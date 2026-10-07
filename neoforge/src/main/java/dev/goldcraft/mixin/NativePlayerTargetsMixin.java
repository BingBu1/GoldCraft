package dev.goldcraft.mixin;

import dev.goldcraft.world.NativePlayers;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.world.EntityView;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import java.util.List;

@Mixin(EntityView.class)
public interface NativePlayerTargetsMixin {
    @Redirect(method={
        "getClosestPlayer(DDDDLjava/util/function/Predicate;)Lnet/minecraft/entity/player/PlayerEntity;",
        "getClosestPlayer(Lnet/minecraft/entity/ai/TargetPredicate;Lnet/minecraft/entity/LivingEntity;)Lnet/minecraft/entity/player/PlayerEntity;",
        "getClosestPlayer(Lnet/minecraft/entity/ai/TargetPredicate;Lnet/minecraft/entity/LivingEntity;DDD)Lnet/minecraft/entity/player/PlayerEntity;",
        "getClosestPlayer(Lnet/minecraft/entity/ai/TargetPredicate;DDD)Lnet/minecraft/entity/player/PlayerEntity;",
        "getPlayerByUuid", "isPlayerInRange", "getPlayers(Lnet/minecraft/entity/ai/TargetPredicate;Lnet/minecraft/entity/LivingEntity;Lnet/minecraft/util/math/Box;)Ljava/util/List;"},
        at=@At(value="INVOKE",target="Lnet/minecraft/world/EntityView;getPlayers()Ljava/util/List;"))
    default List<? extends PlayerEntity> goldcraft$nativeTargets(EntityView view){
        var vanilla=view.getPlayers();
        return view instanceof ServerWorld world?NativePlayers.targetingPlayers(world,vanilla):vanilla;
    }
}
