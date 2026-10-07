package dev.goldcraft.client.mixin;

import dev.goldcraft.GoldCraft;
import net.minecraft.client.network.AbstractClientPlayerEntity;
import net.minecraft.client.render.entity.PlayerEntityRenderer;
import net.neoforged.neoforge.client.event.RenderLivingEvent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Epic Fight 21.17's PPlayerRenderer requires the vanilla PlayerRenderer type. */
@Pseudo
@Mixin(targets="yesman.epicfight.client.events.engine.RenderEngine",remap=false)
abstract class EpicFightPlayerRendererMixin {
    @Unique private boolean goldcraft$reportedCustomPlayer;

    @Inject(method="epicfight$renderLivingPre",at=@At("HEAD"),cancellable=true)
    private void goldcraft$preserveCustomPlayer(RenderLivingEvent.Pre<?,?> event,CallbackInfo ci) {
        if(event.getEntity() instanceof AbstractClientPlayerEntity
                && !(event.getRenderer() instanceof PlayerEntityRenderer)) {
            // Return from this listener, without cancelling the event or the custom renderer.
            // YSM posts a Living event using its own renderer for world, inventory and HUD views.
            if(!goldcraft$reportedCustomPlayer) {
                GoldCraft.LOGGER.info("Preserving custom player renderer {} instead of Epic Fight's incompatible PlayerRenderer cast",
                    event.getRenderer().getClass().getName());
                goldcraft$reportedCustomPlayer=true;
            }
            ci.cancel();
        }
    }
}
