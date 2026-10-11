package dev.goldcraft.client;

import net.minecraft.client.gui.screen.DeathScreen;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.text.Text;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class DeathHudPolicyTest {
    private static Screen ordinaryScreen(){return new Screen(Text.empty()) {};}

    @Test void eitherDeathOrderSuppressesAnExistingMenu() {
        var inventory=ordinaryScreen();
        assertFalse(DeathHudPolicy.useNativeHud(true,1|16|32|64,inventory));
        assertTrue(DeathHudPolicy.useNativeHud(false,1|16|32|64,inventory));
        assertTrue(DeathHudPolicy.useNativeHud(true,16|64,inventory));
        assertTrue(DeathHudPolicy.useNativeHud(false,16|64,null));
    }

    @Test void actualVanillaNormalAndHardcoreDeathScreensStayHiddenThroughRespawn() {
        for(boolean hardcore:new boolean[]{false,true}) {
            var death=new DeathScreen(Text.literal("test death"),hardcore);
            assertTrue(DeathHudPolicy.useNativeHud(false,16|64,death));
            assertTrue(DeathHudPolicy.useNativeHud(true,1|16|64,death));
            assertFalse(DeathHudPolicy.useNativeHud(true,1|16|64,null));
            assertFalse(DeathHudPolicy.useNativeHud(true,1|16|32|64,ordinaryScreen()));
        }
    }

    @Test void aliveFormAndControlLeaseChangesAreNotClassifiedAsDeath() {
        var menu=ordinaryScreen();
        for(int flags:new int[]{1,1|16,1|16|64,1|16|32|64}) {
            assertFalse(DeathHudPolicy.useNativeHud(true,flags,null));
            assertFalse(DeathHudPolicy.useNativeHud(true,flags,menu));
        }
    }
}
