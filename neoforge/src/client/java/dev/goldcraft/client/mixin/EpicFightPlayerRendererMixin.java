package dev.goldcraft.client.mixin;

import dev.goldcraft.GoldCraft;
import net.minecraft.client.network.AbstractClientPlayerEntity;
import net.minecraft.client.render.entity.PlayerEntityRenderer;
import net.minecraft.entity.EntityType;
import net.neoforged.neoforge.client.event.RenderLivingEvent;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import java.util.Map;

/** Epic Fight 21.17's PPlayerRenderer requires the vanilla PlayerRenderer type. */
@Pseudo
@Mixin(targets="yesman.epicfight.client.events.engine.RenderEngine",remap=false)
abstract class EpicFightPlayerRendererMixin {
    @Shadow @Final private Map<EntityType<?>,?> entityRendererCache;
    @Unique private boolean goldcraft$reportedCustomPlayer;
    @Unique private boolean goldcraft$reportedCompatiblePlayer;

    @Unique private static boolean goldcraft$acceptsCustomPlayer(Object renderer) {
        if(renderer==null)return false;
        // EpicYSM 1.1.3 accepts LivingEntityRenderer in its render method; the
        // original Epic Fight PPlayerRenderer requires the vanilla subtype.
        for(Class<?> type=renderer.getClass();type!=null;type=type.getSuperclass()) {
            if(type.getName().equals("com.argorice.epicysm.client.render.EpicYsmPlayerRenderer"))return true;
        }
        return false;
    }

    @Inject(method="epicfight$renderLivingPre",at=@At("HEAD"),cancellable=true)
    private void goldcraft$preserveCustomPlayer(RenderLivingEvent.Pre<?,?> event,CallbackInfo ci) {
        if(event.getEntity() instanceof AbstractClientPlayerEntity
                && !(event.getRenderer() instanceof PlayerEntityRenderer)) {
            var patched=entityRendererCache.get(event.getEntity().getType());
            if(goldcraft$acceptsCustomPlayer(patched)) {
                if(!goldcraft$reportedCompatiblePlayer) {
                    GoldCraft.LOGGER.info("Forwarding custom player renderer {} to compatible {}",
                        event.getRenderer().getClass().getName(),patched.getClass().getName());
                    goldcraft$reportedCompatiblePlayer=true;
                }
                return;
            }
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
