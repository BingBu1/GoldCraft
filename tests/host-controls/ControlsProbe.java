package dev.goldcraft.client;

import dev.goldcraft.bridge.Wire;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.gui.screen.Overlay;
import net.minecraft.client.option.KeyBinding;
import net.neoforged.neoforge.client.settings.KeyModifier;
import org.spongepowered.asm.mixin.injection.callback.*;

public final class ControlsProbe {
    public static int mouseEvents,keyEvents,gunEdges,chords,wheels;public static double scroll;
    public static final KeyBinding chord=new KeyBinding("YSM model",89,false),wheel=new KeyBinding("YSM wheel",90,false);
    static long sequence;static int checks;
    static void check(boolean value){checks++;if(!value)throw new AssertionError("check "+checks);}
    static byte[] key(long epoch,int life,int kind,int code,int action,int mods){
        return new Wire.Writer().i64(epoch).i64(++sequence).i32(life).i32(kind).i32(code).i32(action).i32(mods).f32(kind==3?1:0).toByteArray();
    }
    static void key(int kind,int code,int action,int mods){HostKeys.input(key(11,3,kind,code,action,mods));}
    static CallbackInfo mouseHandler(String part,Object...args)throws Exception{
        var mouse=MinecraftClient.getInstance().mouse;
        var method=java.util.Arrays.stream(mouse.getClass().getDeclaredMethods()).filter(m->m.getName().contains(part)).findFirst().orElseThrow();
        method.setAccessible(true);var ci=new CallbackInfo("fixture",true);
        Object[] all=java.util.Arrays.copyOf(args,args.length+1);all[args.length]=ci;method.invoke(mouse,all);return ci;
    }
    public static void main(String[] args)throws Exception{
        var c=MinecraftClient.getInstance();chord.modifier=KeyModifier.ALT;
        HostInput.control=false;c.windowFocused=false;c.mouse.cursorLocked=false;
        check(!ActualTacz.gate()&&!ActualYsm.gate()); // the original background state
        HostInput.control=true;check(ActualTacz.gate()&&ActualYsm.gate());
        check(!c.windowFocused&&!c.mouse.cursorLocked); // no physical focus/capture changes
        key(1,342,1,4);key(1,89,1,4);check(chords==1&&Screen.hasAltDown());
        key(1,89,0,4);key(1,342,0,0);key(1,89,1,0);check(chords==1&&!Screen.hasAltDown());key(1,89,0,0);
        key(1,90,1,0);check(wheels==1);key(1,90,0,0);
        for(int b=0;b<3;b++)for(int repeat=0;repeat<32;repeat++){
            int before=mouseEvents;key(2,b,1,0);check(HostKeys.mouseDown(b));
            key(2,b,1,0);key(2,b,2,0);check(mouseEvents==before+1);
            key(2,b,0,0);check(!HostKeys.mouseDown(b)&&mouseEvents==before+2);
        }
        check(gunEdges==192);key(3,0,0,0);check(scroll==1);
        key(2,0,1,0);key(2,1,1,0);check(c.options.attackKey.isPressed()&&c.options.useKey.isPressed());
        c.currentScreen=new Screen();check(!ActualTacz.gate()&&!ActualYsm.gate());
        HostKeys.releaseAll();check(!c.options.attackKey.isPressed()&&!c.options.useKey.isPressed());
        int k=keyEvents;key(1,89,1,4);check(keyEvents==k);c.currentScreen=null;
        c.overlay=new Overlay();check(!ActualTacz.gate());c.overlay=null;check(ActualTacz.gate());
        key(1,342,1,4);key(2,0,1,4);HostInput.active=false;
        check(!ActualTacz.gate()&&!ActualYsm.gate());key(1,89,1,4);check(chords==1);
        key(1,342,0,0);key(2,0,0,0);check(!HostKeys.keyDown(342)&&!HostKeys.mouseDown(0));
        HostInput.active=true;k=keyEvents;HostKeys.input(key(12,3,1,90,1,0));HostKeys.input(key(11,4,1,90,1,0));check(keyEvents==k);
        byte[] duplicate=key(11,3,1,90,1,0);HostKeys.input(duplicate);HostKeys.input(duplicate);check(keyEvents==k+1);
        HostKeys.reset();check(!HostKeys.keyDown(90));
        check(mouseHandler("keepHostCapture").isCancelled());
        check(mouseHandler("hostPointer",7L,20.0,30.0).isCancelled());
        c.mouse.cursorDeltaX=12;c.mouse.cursorDeltaY=9;check(mouseHandler("hostMouseTick").isCancelled());
        check(c.mouse.cursorDeltaX==0&&c.mouse.cursorDeltaY==0&&c.mouse.lastTickTime==23);
        HostAudio.physical=true;c.windowFocused=true;c.mouse.cursorLocked=true;check(ActualTacz.gate()&&ActualYsm.gate());
        check(!mouseHandler("keepHostCapture").isCancelled()&&!mouseHandler("hostMouseTick").isCancelled());
        c.mouse.cursorLocked=false;check(!ActualTacz.gate());HostInput.control=false;HostAudio.physical=false;
        c.windowFocused=false;check(!ActualYsm.gate());HostKeys.reset();
        System.out.println("PASS "+checks+" checks: actual gate/chord bytecode, ALT+Y/Z, 96 mouse cycles, duplicate/repeat/release, menu/overlay/lease/identity/reset, physical focus fallback, no physical cursor capture or duplicate look");
    }
}
