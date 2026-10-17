package dev.goldcraft.client;

import net.minecraft.client.MinecraftClient;

/** Logical input focus for mods while the paired CS window owns the controls. */
public final class HostFocus {
    private HostFocus(){}
    public static boolean remote(){
        return HostInput.controlling()&&!HostAudio.standaloneFocused(MinecraftClient.getInstance());
    }
    public static boolean focused(){return HostInput.active();}
    public static boolean captured(){
        var client=MinecraftClient.getInstance();
        return focused()&&client.currentScreen==null&&client.getOverlay()==null;
    }
}
