package dev.goldcraft.mixin;

import dev.goldcraft.world.SharedVitals;
import net.minecraft.network.packet.c2s.play.ClientStatusC2SPacket;
import net.minecraft.server.network.ServerPlayNetworkHandler;
import net.minecraft.server.network.ServerPlayerEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ServerPlayNetworkHandler.class)
public abstract class SharedVitalsRespawnMixin {
    @Shadow public ServerPlayerEntity player;
    @Inject(method="onClientStatus",at=@At(value="INVOKE",target="Lnet/minecraft/server/network/ServerPlayerEntity;updateLastActionTime()V"),cancellable=true)
    private void goldcraft$nativeRespawn(ClientStatusC2SPacket packet,CallbackInfo ci){
        // This injection is after vanilla's main-thread handoff.
        if(packet.getMode()==ClientStatusC2SPacket.Mode.PERFORM_RESPAWN&&!player.notInAnyWorld&&SharedVitals.nativeRespawnOwned(player))ci.cancel();
    }
}
