package dev.goldcraft.client;

import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.FramePacer;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.client.mixin.GameRendererAccessor;
import com.mojang.blaze3d.systems.RenderSystem;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.entity.effect.StatusEffects;
import net.minecraft.util.math.MathHelper;

/** Reuse vanilla's authoritative-effect mirror and client accessibility setting. */
final class ViewDistortionExporter {
    private static final FramePacer pacing=new FramePacer(60);
    private static long revision;
    private ViewDistortionExporter(){}
    static void frame(MinecraftClient client,BridgeLink link){
        long generation=link.generation();
        if(!link.viewDistortion(generation)||!HostInput.hosted()||HostInput.life()==0||!pacing.ready(System.nanoTime()))return;
        var actor=GoldCraftClient.HOST.actor(client.player.getUuid());
        if(actor==null||actor.life()!=HostInput.life())return;
        float delta=client.getRenderTickCounter().getTickDelta(false);
        float scale=client.options.getDistortionEffectScale().getValue().floatValue();
        float intensity=client.player.isAlive()&&!HudExporter.nativeDeathHud(client)
            ?MathHelper.lerp(delta,client.player.prevNauseaIntensity,client.player.nauseaIntensity)*scale*scale:0;
        int speed=client.player.hasStatusEffect(StatusEffects.NAUSEA)?7:20;
        // Preserve vanilla float tick arithmetic before reducing the angle for transport.
        float angle=(((GameRendererAccessor)client.gameRenderer).goldcraft$ticks()+delta)*speed*(float)(Math.PI/180);
        angle=(float)Math.IEEEremainder(angle,Math.PI*2);
        link.sendViewDistortion(generation,new Wire.Writer().i64(GoldCraftClient.HOST.epoch()).i64(++revision)
            .i32(actor.serial()).i32(actor.life()).f32(intensity).f32(angle).toByteArray());
    }
    static void overlay(MinecraftClient client,DrawContext context,float delta){
        if(!client.player.isAlive()||HudExporter.nativeDeathHud(client))return;
        float intensity=MathHelper.lerp(delta,client.player.prevNauseaIntensity,client.player.nauseaIntensity);
        float scale=client.options.getDistortionEffectScale().getValue().floatValue();
        if(intensity>0&&client.player.hasStatusEffect(StatusEffects.NAUSEA)&&scale<1){
            // Vanilla uses ONE/ONE additive blending, not alpha coverage. Keep
            // RGB at alpha zero so the premultiplied CS HUD compositor adds it
            // to the host scene without hiding the map. Flush within the mask.
            context.draw();RenderSystem.colorMask(true,true,true,false);
            try {
                ((GameRendererAccessor)client.gameRenderer).goldcraft$renderNausea(context,intensity*(1-scale));
                context.draw();
            }finally{RenderSystem.colorMask(true,true,true,true);}
        }
    }
}
