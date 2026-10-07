package dev.goldcraft.bridge;

/** Minecraft 1.21.1 OverlayTexture, stored as RGBA with blend weight in alpha. */
public final class OverlayColor {
    private OverlayColor() {}
    public static int fromUv(int u, int v) {
        if (v >= 0 && v < 8) return 0x4d0000ff;
        int x = Math.clamp(u, 0, 15);
        int weight = 255 - (int)((1.0f - x / 15.0f * 0.75f) * 255.0f);
        return weight << 24 | 0x00ffffff;
    }
}
