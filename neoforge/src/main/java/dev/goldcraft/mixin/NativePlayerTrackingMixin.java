package dev.goldcraft.mixin;

import dev.goldcraft.world.NativePlayerEntity;
import net.minecraft.entity.Entity;
import net.minecraft.server.world.ServerChunkLoadingManager;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ServerChunkLoadingManager.class)
public abstract class NativePlayerTrackingMixin {
    @Inject(method="loadEntity",at=@At("HEAD"),cancellable=true)
    private void goldcraft$nativeBody(Entity entity,CallbackInfo ci){
        // Native GoldSrc already draws this body. It is an AI/physics target on
        // the MC server, not a second network player or another rendered avatar.
        if(entity instanceof NativePlayerEntity)ci.cancel();
    }
}
