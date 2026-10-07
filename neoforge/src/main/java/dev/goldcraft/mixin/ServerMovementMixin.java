package dev.goldcraft.mixin;

import dev.goldcraft.world.HostCollision;
import dev.goldcraft.world.HostMovement;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import java.util.List;

@Mixin(ServerPlayNetworkHandler.class)
public abstract class ServerMovementMixin {
    @Inject(method="isEntityOnAir",at=@At("RETURN"),cancellable=true)
    private void goldcraft$hostSupportsPlayer(Entity entity,CallbackInfoReturnable<Boolean> result) {
        if(result.getReturnValueZ()) {
            if(HostMovement.supported(entity)){result.setReturnValue(false);return;}
            if(entity instanceof LivingEntity living&&HostMovement.ladder(living)!=null){result.setReturnValue(false);return;}
            var below=entity.getBoundingBox().expand(0.0625).stretch(0,-0.55,0);
            for(var shape:HostCollision.augment(entity.getWorld(),below,List.of()))if(!shape.isEmpty()){result.setReturnValue(false);break;}
        }
    }
}
