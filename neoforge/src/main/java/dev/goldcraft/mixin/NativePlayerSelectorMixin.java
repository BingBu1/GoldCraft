package dev.goldcraft.mixin;

import dev.goldcraft.world.NativePlayers;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.server.world.ServerWorld;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import java.util.List;
import java.util.function.Predicate;

/** Trial spawners and vaults share this selector; custom selectors stay intact. */
@Mixin(targets="net.minecraft.block.spawner.EntityDetector$Selector$1")
abstract class NativePlayerSelectorMixin {
    @Inject(method="getPlayers",at=@At("RETURN"),cancellable=true)
    private void goldcraft$nativePlayers(ServerWorld world,Predicate<? super PlayerEntity> predicate,
                                        CallbackInfoReturnable<List<? extends PlayerEntity>> result){
        var vanilla=result.getReturnValue();
        var combined=NativePlayers.targetingPlayers(world,vanilla);
        if(combined!=vanilla)result.setReturnValue(combined.stream().filter(predicate).toList());
    }
}
