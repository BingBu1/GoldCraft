package dev.goldcraft.mixin;

import dev.goldcraft.world.NativePlayers;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.ai.brain.sensor.NearestPlayersSensor;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.predicate.entity.EntityPredicates;
import net.minecraft.server.world.ServerWorld;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
import java.util.Comparator;
import java.util.List;

/** Preserve the original visibility/attackability pipeline after player discovery. */
@Mixin(NearestPlayersSensor.class)
abstract class NativePlayerSensorMixin {
    @ModifyVariable(method="sense",at=@At("STORE"),ordinal=0)
    private List<PlayerEntity> goldcraft$nativePlayers(List<PlayerEntity> vanilla,ServerWorld world,LivingEntity observer){
        var combined=NativePlayers.targetingPlayers(world,vanilla);
        if(combined==vanilla)return vanilla;
        return combined.stream().filter(EntityPredicates.EXCEPT_SPECTATOR)
            .filter(player->observer.isInRange(player,16))
            .sorted(Comparator.comparingDouble(observer::squaredDistanceTo))
            .map(player->(PlayerEntity)player).toList();
    }
}
