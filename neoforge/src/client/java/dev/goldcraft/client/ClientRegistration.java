package dev.goldcraft.client;

import dev.goldcraft.world.NativePlayerHitboxEntity;
import net.minecraft.client.render.entity.EmptyEntityRenderer;
import net.neoforged.api.distmarker.Dist;
import net.neoforged.bus.api.SubscribeEvent;
import net.neoforged.fml.common.EventBusSubscriber;
import net.neoforged.fml.event.lifecycle.FMLClientSetupEvent;
import net.neoforged.neoforge.client.event.EntityRenderersEvent;

@EventBusSubscriber(modid="goldcraft",value=Dist.CLIENT,bus=EventBusSubscriber.Bus.MOD)
public final class ClientRegistration {
    private ClientRegistration() {}
    @SubscribeEvent public static void setup(FMLClientSetupEvent event) {
        event.enqueueWork(()->new GoldCraftClient().initialize());
    }
    @SubscribeEvent public static void renderers(EntityRenderersEvent.RegisterRenderers event) {
        event.registerEntityRenderer(NativePlayerHitboxEntity.TYPE.get(),EmptyEntityRenderer::new);
    }
}
