package dev.goldcraft.client;

import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.util.Identifier;

/** Vanilla golden hearts above the native ammunition HUD. One heart is two HP. */
final class NativeAbsorptionHud {
    private static final Identifier FULL=Identifier.ofVanilla("hud/heart/absorbing_full");
    private static final Identifier HALF=Identifier.ofVanilla("hud/heart/absorbing_half");
    private NativeAbsorptionHud(){}
    static void draw(DrawContext context,MinecraftClient client,int width,int height,float health){
        int points=(int)Math.ceil(health),hearts=Math.min(10,(points+1)/2),x=width-8-hearts*9,y=height-48;
        for(int i=0;i<hearts;i++)context.drawGuiTexture(points-i*2>=2?FULL:HALF,x+i*9,y,9,9);
        if(points>20)context.drawTextWithShadow(client.textRenderer,"+"+points,width-8-client.textRenderer.getWidth("+"+points),y-11,0xffd34e);
    }
}
