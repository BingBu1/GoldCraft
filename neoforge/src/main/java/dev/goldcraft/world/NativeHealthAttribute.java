package dev.goldcraft.world;

/** Per-instance ownership; never changes the global Minecraft attribute. */
public interface NativeHealthAttribute {
    void goldcraft$healthOwnership(boolean owned,boolean restoreBase);
    boolean goldcraft$healthOwned();
    double goldcraft$originalHealthBase();
}
