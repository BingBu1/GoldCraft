package dev.goldcraft.mixin;

import dev.goldcraft.world.NativePlayers;
import net.minecraft.entity.Entity;
import net.minecraft.server.world.ServerWorld;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import java.util.UUID;

/** UUID owners/anger can resolve the logical body without a second area target. */
@Mixin(ServerWorld.class)
public abstract class NativePlayerLookupMixin {
    @Inject(method="getEntity(Ljava/util/UUID;)Lnet/minecraft/entity/Entity;",at=@At("RETURN"),cancellable=true)
    private void goldcraft$nativeIdentity(UUID uuid,CallbackInfoReturnable<Entity> result){
        var body=NativePlayers.byUuid((ServerWorld)(Object)this,uuid);
        if(body!=null)result.setReturnValue(body);
    }
}
