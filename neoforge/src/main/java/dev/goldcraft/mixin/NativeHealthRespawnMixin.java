package dev.goldcraft.mixin;

import dev.goldcraft.world.NativeHealth;
import net.minecraft.server.network.ServerPlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ServerPlayerEntity.class)
abstract class NativeHealthRespawnMixin {
    @Inject(method="copyFrom",at=@At(value="INVOKE",target="Lnet/minecraft/entity/attribute/AttributeContainer;setBaseFrom(Lnet/minecraft/entity/attribute/AttributeContainer;)V",shift=At.Shift.AFTER))
    private void goldcraft$originalHealthBase(ServerPlayerEntity oldPlayer,boolean alive,CallbackInfo ci){
        NativeHealth.restoreCopiedBase(oldPlayer,(ServerPlayerEntity)(Object)this);
    }
}
