package dev.goldcraft.mixin;

import dev.goldcraft.world.EpicSkillSync;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Coerce;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Pseudo
@Mixin(targets="yesman.epicfight.world.capabilities.entitypatch.player.ServerPlayerPatch",remap=false)
abstract class EpicSkillJoinMixin {
    @Inject(method="onJoinWorld(Lnet/minecraft/server/level/ServerPlayer;Lnet/minecraft/world/level/Level;Z)V",at=@At("TAIL"),require=1)
    private void goldcraft$bindTransientSkills(@Coerce Object player,@Coerce Object level,boolean worldgen,CallbackInfo ci) {
        EpicSkillSync.joined(this);
    }
}
