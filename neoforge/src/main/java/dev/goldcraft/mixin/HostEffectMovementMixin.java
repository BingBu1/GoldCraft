package dev.goldcraft.mixin;

import dev.goldcraft.world.HostPlayerEffects;
import net.minecraft.network.packet.c2s.play.PlayerMoveC2SPacket;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.network.packet.s2c.play.PositionFlag;
import org.objectweb.asm.Opcodes;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import java.util.Set;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;

// Pinned Yarn 1.21.1 names. Production packaging must generate/remap the refmap.
@Mixin(ServerPlayNetworkHandler.class)
public abstract class HostEffectMovementMixin {
    @Shadow public ServerPlayerEntity player;
    @Inject(method="onPlayerMove",at=@At(value="INVOKE",target="Lnet/minecraft/server/network/ServerPlayerEntity;hasVehicle()Z"),cancellable=true,require=1,allow=1)
    private void goldcraft$gate(PlayerMoveC2SPacket packet, CallbackInfo ci) {
        // Vanilla has already rescheduled to its server thread, rejected NaN /
        // invalid rotation, checked world state and processed pending teleports.
        if (HostPlayerEffects.rejectMove(player,packet)) ci.cancel();
    }
    @Inject(method="onPlayerMove",at=@At(value="FIELD",target="Lnet/minecraft/server/network/ServerPlayNetworkHandler;updatedZ:D",opcode=Opcodes.PUTFIELD,shift=At.Shift.AFTER),require=1,allow=1)
    private void goldcraft$accepted(PlayerMoveC2SPacket packet, CallbackInfo ci) {
        HostPlayerEffects.accepted(player,packet);
    }
    @WrapOperation(method="onPlayerMove",at=@At(value="INVOKE",target="Lnet/minecraft/server/network/ServerPlayNetworkHandler;requestTeleport(DDDFF)V"),require=3,allow=3)
    private void goldcraft$correction(ServerPlayNetworkHandler handler,double x,double y,double z,float yaw,float pitch,Operation<Void> original) {
        HostPlayerEffects.vanillaCorrection(player,()->original.call(handler,x,y,z,yaw,pitch));
    }
    @WrapOperation(method="handlePendingTeleport",at=@At(value="INVOKE",target="Lnet/minecraft/server/network/ServerPlayNetworkHandler;requestTeleport(DDDFF)V"),require=1,allow=1)
    private void goldcraft$resendCorrection(ServerPlayNetworkHandler handler,double x,double y,double z,float yaw,float pitch,Operation<Void> original) {
        HostPlayerEffects.vanillaCorrection(player,()->original.call(handler,x,y,z,yaw,pitch));
    }
    @Inject(method="requestTeleport(DDDFFLjava/util/Set;)V",at=@At("HEAD"),require=1)
    private void goldcraft$requestTeleport(double x,double y,double z,float yaw,float pitch,Set<PositionFlag> flags,CallbackInfo ci) {
        HostPlayerEffects.teleporting(player,x,y,z);
    }
}
