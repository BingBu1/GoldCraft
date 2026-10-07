package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.bridge.Wire;
import net.minecraft.client.MinecraftClient;

/** GUI events target the screen generation the CS client actually displayed. */
public final class HostUi {
    private static HudExporter hud;
    private static double x,y;
    private static int held,modifiers;
    private static long accepted,rejected;
    private static boolean dispatching;
    private HostUi(){}
    public static void configure(HudExporter value){hud=value;}
    public static void reset(){held=modifiers=0;x=hud==null?0:hud.width()/2.0;y=hud==null?0:hud.height()/2.0;}
    public static double mouseX(){return x;}
    public static double mouseY(){return y;}
    public static boolean dispatching(){return dispatching;}
    public static boolean modifier(int mask){return (modifiers&mask)!=0;}
    public static void input(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload);long epoch=r.i64(),menu=r.i64();int type=r.i32(),code=r.i32(),action=r.i32(),mods=r.i32();
        float nx=r.f32(),ny=r.f32(),amount=r.f32();r.finish();
        if(type<1||type>5||action<0||action>2||(mods&~7)!=0||nx<0||nx>1||ny<0||ny>1||Math.abs(amount)>16)
            throw new IllegalArgumentException("Host UI input bounds");
        var client=MinecraftClient.getInstance();
        if(hud==null||!HostInput.hosted()||epoch!=GoldCraftClient.HOST.epoch()||menu==0||menu!=hud.menuId()
            ||hud.screen()!=client.currentScreen||client.currentScreen==null){rejected++;return;}
        var screen=client.currentScreen;double nextX=nx*hud.width(),nextY=ny*hud.height(),dx=nextX-x,dy=nextY-y;
        x=nextX;y=nextY;modifiers=mods;dispatching=true;
        try {
            switch(type) {
                case 1 -> {screen.mouseMoved(x,y);for(int button=0;button<3;button++)if((held&(1<<button))!=0)screen.mouseDragged(x,y,button,dx,dy);}
                case 2 -> {
                    if(code<0||code>2)throw new IllegalArgumentException("Host UI button");
                    if(action!=0){held|=1<<code;screen.mouseClicked(x,y,code);}else{held&=~(1<<code);screen.mouseReleased(x,y,code);}
                }
                case 3 -> screen.mouseScrolled(x,y,0,amount);
                case 4 -> {
                    if(code<32||code>348)throw new IllegalArgumentException("Host UI key");
                    if(action!=0)screen.keyPressed(code,0,mods);else screen.keyReleased(code,0,mods);
                }
                case 5 -> {if(code<32||code>65535||Character.isSurrogate((char)code))throw new IllegalArgumentException("Host UI character");screen.charTyped((char)code,mods);}
            }
            accepted++;
        }finally{dispatching=false;}
    }
    public static void diagnostics(JsonObject data){data.addProperty("uiAccepted",accepted);data.addProperty("uiRejected",rejected);data.addProperty("uiMouseX",x);data.addProperty("uiMouseY",y);}
}
