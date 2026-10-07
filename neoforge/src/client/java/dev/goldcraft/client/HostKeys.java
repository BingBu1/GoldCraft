package dev.goldcraft.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.client.mixin.MouseAccessor;
import java.util.HashSet;
import java.util.Set;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.util.InputUtil;
import org.lwjgl.glfw.GLFW;

/** Real Minecraft/NeoForge key events plus an isolated virtual physical-key state. */
public final class HostKeys {
    private static final Set<Integer> keys=new HashSet<>(),mouse=new HashSet<>();
    private static long epoch,sequence,accepted,rejected;
    private static int life,modifiers;
    private static boolean dispatching,attackEvent,useEvent;
    private HostKeys(){}
    public static boolean dispatching(){return dispatching;}
    public static boolean overrides(long window){
        var client=MinecraftClient.getInstance();
        return window==client.getWindow().getHandle()&&HostInput.controlling()&&!HostAudio.standaloneFocused(client);
    }
    public static boolean keyDown(int code){return keys.contains(code);}
    public static boolean mouseDown(int code){return mouse.contains(code);}
    public static void input(byte[] payload){
        var r=new Wire.Reader(payload);long nextEpoch=r.i64(),next=r.i64();
        int nextLife=r.i32(),kind=r.i32(),code=r.i32(),action=r.i32(),mods=r.i32();float amount=r.f32();r.finish();
        if(next==0||kind<0||kind>3||action<0||action>2||(mods&~7)!=0||Math.abs(amount)>16
            ||kind==1&&(code<32||code>348)||kind==2&&(code<0||code>7))throw new IllegalArgumentException("Host key bounds");
        if(nextEpoch!=GoldCraftClient.HOST.epoch()||nextLife!=HostInput.life()||!HostInput.controlling()
            ||epoch==nextEpoch&&life==nextLife&&next<=sequence){rejected++;return;}
        if(epoch!=nextEpoch||life!=nextLife){reset();epoch=nextEpoch;life=nextLife;}
        sequence=next;
        if(kind==0){releaseAll();accepted++;return;}
        var client=MinecraftClient.getInstance();
        if(client.currentScreen!=null||!HostInput.active()){rejected++;return;}
        modifiers=mods;
        if(kind==1){if(action==0)keys.remove(code);else keys.add(code);}
        if(kind==2){if(action==0)mouse.remove(code);else mouse.add(code);}
        dispatching=true;
        try{
            if(kind==1){
                int scan=GLFW.glfwGetKeyScancode(code);
                if(action==1){attackEvent|=client.options.attackKey.matchesKey(code,scan);useEvent|=client.options.useKey.matchesKey(code,scan);}
                client.keyboard.onKey(client.getWindow().getHandle(),code,scan,action,mods);
            }else if(kind==2){
                if(action==1){attackEvent|=client.options.attackKey.matchesMouse(code);useEvent|=client.options.useKey.matchesMouse(code);}
                ((MouseAccessor)client.mouse).goldcraft$button(client.getWindow().getHandle(),code,action,mods);
            }else ((MouseAccessor)client.mouse).goldcraft$scroll(client.getWindow().getHandle(),0,amount);
            accepted++;
        }finally{dispatching=false;}
    }
    public static boolean attackEvent(){return attackEvent;}
    public static boolean useEvent(){return useEvent;}
    public static void endTick(){attackEvent=useEvent=false;}
    public static void releaseAll(){
        var client=MinecraftClient.getInstance();dispatching=true;
        try{
            for(int code:Set.copyOf(keys)){
                keys.remove(code);
                client.keyboard.onKey(client.getWindow().getHandle(),code,GLFW.glfwGetKeyScancode(code),GLFW.GLFW_RELEASE,0);
            }
            for(int code:Set.copyOf(mouse)){
                mouse.remove(code);
                ((MouseAccessor)client.mouse).goldcraft$button(client.getWindow().getHandle(),code,GLFW.GLFW_RELEASE,0);
            }
        }finally{keys.clear();mouse.clear();modifiers=0;dispatching=false;attackEvent=useEvent=false;}
    }
    public static void reset(){releaseAll();epoch=sequence=0;life=0;}
    static void diagnostics(JsonObject data){
        var value=new JsonObject();value.addProperty("accepted",accepted);value.addProperty("rejected",rejected);
        value.addProperty("heldKeys",keys.size());value.addProperty("heldMouse",mouse.size());value.addProperty("modifiers",modifiers);
        var bindings=new JsonArray();
        for(var key:MinecraftClient.getInstance().options.allKeys){
            var entry=new JsonObject();entry.addProperty("name",key.getTranslationKey());entry.addProperty("key",key.getBoundKeyTranslationKey());
            entry.addProperty("pressed",key.isPressed());bindings.add(entry);
        }
        value.add("bindings",bindings);data.add("hostKeys",value);
    }
}
