package dev.goldcraft.client;
import dev.goldcraft.bridge.Wire;
import net.minecraft.client.MinecraftClient;
public class HostUiProbe {
    static byte[] event(long world,long menu,int type,int code,int mods){
        return new Wire.Writer().i64(world).i64(menu).i32(type).i32(code).i32(1).i32(mods).f32(.5f).f32(.5f).f32(0).toByteArray();
    }
    static void text(int cp){HostUi.input(event(11,21,5,cp,0));}
    public static void main(String[] args){
        var client=MinecraftClient.getInstance();var screen=client.currentScreen;HostUi.configure(new HudExporter());
        text(0x4e2d);text(0x6587);HostUi.input(event(11,21,5,'@',1));
        for(char c:"0123456789+./".toCharArray())text(c);
        text(0x1f600);assert screen.text.toString().equals("中文@0123456789+./😀");
        int length=screen.text.length();
        for(int bad:new int[]{-1,0,31,127,0xd800,0xdfff,0x110000}){
            boolean rejected=false;try{text(bad);}catch(IllegalArgumentException e){rejected=true;}assert rejected;
            assert !HostUi.dispatching()&&screen.text.length()==length;
        }
        HostUi.input(event(12,21,5,'x',0));HostUi.input(event(11,22,5,'x',0));
        HostInput.enabled=false;text('x');HostInput.enabled=true;
        var replacement=new MinecraftClient.Screen();client.currentScreen=replacement;text('x');client.currentScreen=screen;
        assert screen.text.length()==length&&replacement.text.isEmpty();
        HostUi.input(event(11,21,4,86,2));assert screen.key==86&&screen.mods==2&&screen.sawControl;
        text('!');assert screen.text.length()==length;
        text('?');assert screen.text.length()==length+1&&net.neoforged.neoforge.client.ClientHooks.post==1;
        assert !HostUi.dispatching();
        System.out.println("PASS production HostUi/Wire: Chinese, symbols, keypad text, UTF-16 pair, invalid scalars, stale world/menu/screen, inactive host and Ctrl shortcuts");
    }
}
