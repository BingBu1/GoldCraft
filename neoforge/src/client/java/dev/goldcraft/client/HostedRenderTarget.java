package dev.goldcraft.client;

import com.mojang.blaze3d.systems.RenderSystem;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.Framebuffer;
import net.minecraft.client.gl.SimpleFramebuffer;

/** Give offscreen hand/UI renderers the same target contract as a normal game frame. */
public final class HostedRenderTarget implements AutoCloseable {
    private static Framebuffer active;
    private final Framebuffer previous;

    static SimpleFramebuffer create(int width,int height) {
        var target=new SimpleFramebuffer(width,height,true,MinecraftClient.IS_SYSTEM_MAC);
        target.enableStencil();
        target.setClearColor(0,0,0,0);
        return target;
    }

    static HostedRenderTarget begin(Framebuffer target) {
        RenderSystem.assertOnRenderThread();
        return new HostedRenderTarget(target);
    }

    private HostedRenderTarget(Framebuffer target) {previous=active;active=target;}

    public static Framebuffer current() {return RenderSystem.isOnRenderThread()?active:null;}

    @Override public void close() {active=previous;}
}
