package dev.goldcraft.client;

import net.minecraft.client.gui.screen.DeathScreen;
import net.minecraft.client.gui.screen.Screen;

/** Death presentation is independent of the player's persistent CS/MC form. */
final class DeathHudPolicy {
    private DeathHudPolicy() {}

    static boolean useNativeHud(boolean minecraftAlive,int hostFlags,Screen screen) {
        // Either authority can report death before the other process catches up.
        // A lingering vanilla death screen must never be exported after respawn.
        return !minecraftAlive||(hostFlags&1)==0||screen instanceof DeathScreen;
    }
}
