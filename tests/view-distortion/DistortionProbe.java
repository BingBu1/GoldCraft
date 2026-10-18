package dev.goldcraft.client;
import dev.goldcraft.bridge.*;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;

public final class DistortionProbe {
    private static int checks;
    private static void check(boolean value){checks++;if(!value)throw new AssertionError(checks);}
    private static void near(float a,float b){check(Math.abs(a-b)<.00001f);}
    private static void frame(MinecraftClient c,BridgeLink link)throws Exception{Thread.sleep(18);ViewDistortionExporter.frame(c,link);}
    public static void main(String[] args)throws Exception{
        var c=new MinecraftClient();var link=new BridgeLink();var context=new DrawContext();long revision=0;
        for(boolean nausea:new boolean[]{false,true})for(double scale:new double[]{0,.25,.5,1})for(float delta:new float[]{0,.5f,1}){
            c.player.nausea=nausea;c.options.scale=scale;c.delta=delta;c.player.prevNauseaIntensity=.2f;c.player.nauseaIntensity=.8f;c.gameRenderer.ticks=57;
            frame(c,link);check(link.last.length==32);var r=new Wire.Reader(link.last);check(r.i64()==99);long next=r.i64();check(next>revision);revision=next;check(r.i32()==7&&r.i32()==3);
            float intensity=.2f+delta*(.8f-.2f);near(r.f32(),intensity*(float)scale*(float)scale);
            near(r.f32(),(float)Math.IEEEremainder((57+delta)*(nausea?7:20)*(float)(Math.PI/180),Math.PI*2));r.finish();
            c.gameRenderer.calls=0;ViewDistortionExporter.overlay(c,context,delta);
            check(com.mojang.blaze3d.systems.RenderSystem.alpha);check(c.gameRenderer.calls==(nausea&&scale<1?1:0));if(c.gameRenderer.calls>0)near(c.gameRenderer.overlay,intensity*(1-(float)scale));
        }
        c.player.alive=false;frame(c,link);var r=new Wire.Reader(link.last);r.bytes(24);near(r.f32(),0);c.gameRenderer.calls=0;ViewDistortionExporter.overlay(c,context,1);check(c.gameRenderer.calls==0);c.player.alive=true;
        HudExporter.dead=true;frame(c,link);r=new Wire.Reader(link.last);r.bytes(24);near(r.f32(),0);HudExporter.dead=false;
        int before=link.sent;HostInput.hosted=false;frame(c,link);check(link.sent==before);HostInput.hosted=true;
        HostInput.life=0;frame(c,link);check(link.sent==before);HostInput.life=4;frame(c,link);check(link.sent==before);HostInput.life=3;
        link.enabled=false;frame(c,link);check(link.sent==before);link.enabled=true;
        GoldCraftClient.HOST.present=false;frame(c,link);check(link.sent==before);GoldCraftClient.HOST.present=true;
        c.player.nausea=true;c.options.scale=.5;frame(c,link);check(link.sent==before+1);
        c.gameRenderer.fail=true;boolean threw=false;try{ViewDistortionExporter.overlay(c,context,1);}catch(IllegalStateException expected){threw=true;}check(threw);check(com.mojang.blaze3d.systems.RenderSystem.alpha);
        System.out.println("PASS "+checks+" production exporter/accessibility/overlay/life/disconnect checks (Minecraft observation adapters)");
    }
}
