package dev.goldcraft.client;

import com.mojang.blaze3d.systems.RenderSystem;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL20;

/** Stencil belongs to the offscreen pass, including clear/write masks and both faces. */
final class HostedStencilState implements AutoCloseable {
    private final boolean enabled=GL11.glIsEnabled(GL11.GL_STENCIL_TEST);
    private final int clear=GL11.glGetInteger(GL11.GL_STENCIL_CLEAR_VALUE);
    private final int function=GL11.glGetInteger(GL11.GL_STENCIL_FUNC),reference=GL11.glGetInteger(GL11.GL_STENCIL_REF),mask=GL11.glGetInteger(GL11.GL_STENCIL_VALUE_MASK);
    private final int write=GL11.glGetInteger(GL11.GL_STENCIL_WRITEMASK),fail=GL11.glGetInteger(GL11.GL_STENCIL_FAIL),depthFail=GL11.glGetInteger(GL11.GL_STENCIL_PASS_DEPTH_FAIL),pass=GL11.glGetInteger(GL11.GL_STENCIL_PASS_DEPTH_PASS);
    private final int backFunction=GL11.glGetInteger(GL20.GL_STENCIL_BACK_FUNC),backReference=GL11.glGetInteger(GL20.GL_STENCIL_BACK_REF),backMask=GL11.glGetInteger(GL20.GL_STENCIL_BACK_VALUE_MASK);
    private final int backWrite=GL11.glGetInteger(GL20.GL_STENCIL_BACK_WRITEMASK),backFail=GL11.glGetInteger(GL20.GL_STENCIL_BACK_FAIL),backDepthFail=GL11.glGetInteger(GL20.GL_STENCIL_BACK_PASS_DEPTH_FAIL),backPass=GL11.glGetInteger(GL20.GL_STENCIL_BACK_PASS_DEPTH_PASS);

    static void prepare() {
        GL11.glDisable(GL11.GL_STENCIL_TEST);
        RenderSystem.stencilMask(255);RenderSystem.stencilFunc(GL11.GL_ALWAYS,0,255);
        RenderSystem.stencilOp(GL11.GL_KEEP,GL11.GL_KEEP,GL11.GL_KEEP);
        GL20.glStencilMaskSeparate(GL11.GL_BACK,255);GL20.glStencilFuncSeparate(GL11.GL_BACK,GL11.GL_ALWAYS,0,255);
        GL20.glStencilOpSeparate(GL11.GL_BACK,GL11.GL_KEEP,GL11.GL_KEEP,GL11.GL_KEEP);
        RenderSystem.clearStencil(0);
        RenderSystem.clear(GL11.GL_STENCIL_BUFFER_BIT,false);
    }

    @Override public void close() {
        RenderSystem.stencilMask(write);RenderSystem.stencilFunc(function,reference,mask);
        RenderSystem.stencilOp(fail,depthFail,pass);RenderSystem.clearStencil(clear);
        GL20.glStencilMaskSeparate(GL11.GL_BACK,backWrite);GL20.glStencilFuncSeparate(GL11.GL_BACK,backFunction,backReference,backMask);
        GL20.glStencilOpSeparate(GL11.GL_BACK,backFail,backDepthFail,backPass);
        if(enabled)GL11.glEnable(GL11.GL_STENCIL_TEST);else GL11.glDisable(GL11.GL_STENCIL_TEST);
    }
}
