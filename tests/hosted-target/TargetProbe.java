package dev.goldcraft.client;

import com.mojang.blaze3d.systems.RenderSystem;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.Framebuffer;
import net.minecraft.client.gl.SimpleFramebuffer;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.opengl.GL;
import static org.lwjgl.opengl.GL33C.*;
import java.util.Arrays;

/** Actual game framebuffer, actual TACZ helper and production target scope in a hidden GPU context. */
public final class TargetProbe {
    static int checks,program,vao;
    static void check(boolean v,String why){checks++;if(!v)throw new AssertionError(why);}
    static int shader(int type,String src){int s=glCreateShader(type);glShaderSource(s,src);glCompileShader(s);check(glGetShaderi(s,GL_COMPILE_STATUS)==GL_TRUE,glGetShaderInfoLog(s));return s;}
    static void initShader(){
        int vs=shader(GL_VERTEX_SHADER,"#version 330\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}");
        int fs=shader(GL_FRAGMENT_SHADER,"#version 330\nuniform vec2 size;uniform int maskPass;out vec4 outColor;void main(){vec2 p=gl_FragCoord.xy/size;if(maskPass==1&&distance(p,vec2(.67,.44))>.19)discard;outColor=vec4(0,0,0,1);}");
        program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);check(glGetProgrami(program,GL_LINK_STATUS)==GL_TRUE,glGetProgramInfoLog(program));glDeleteShader(vs);glDeleteShader(fs);vao=glGenVertexArrays();
    }
    static int[] stencilState(){return new int[]{glIsEnabled(GL_STENCIL_TEST)?1:0,glGetInteger(GL_STENCIL_CLEAR_VALUE),glGetInteger(GL_STENCIL_FUNC),glGetInteger(GL_STENCIL_REF),glGetInteger(GL_STENCIL_VALUE_MASK),glGetInteger(GL_STENCIL_WRITEMASK),glGetInteger(GL_STENCIL_FAIL),glGetInteger(GL_STENCIL_PASS_DEPTH_FAIL),glGetInteger(GL_STENCIL_PASS_DEPTH_PASS),glGetInteger(GL_STENCIL_BACK_FUNC),glGetInteger(GL_STENCIL_BACK_REF),glGetInteger(GL_STENCIL_BACK_VALUE_MASK),glGetInteger(GL_STENCIL_BACK_WRITEMASK),glGetInteger(GL_STENCIL_BACK_FAIL),glGetInteger(GL_STENCIL_BACK_PASS_DEPTH_FAIL),glGetInteger(GL_STENCIL_BACK_PASS_DEPTH_PASS)};}
    static void hostileStencil(){
        glEnable(GL_STENCIL_TEST);RenderSystem.clearStencil(93);RenderSystem.stencilMask(0);RenderSystem.stencilFunc(GL_NEVER,7,0x3f);RenderSystem.stencilOp(GL_INCR,GL_DECR,GL_INVERT);
        glStencilFuncSeparate(GL_BACK,GL_LESS,12,0xa5);glStencilMaskSeparate(GL_BACK,0x61);glStencilOpSeparate(GL_BACK,GL_ZERO,GL_REPLACE,GL_INCR_WRAP);
    }
    static byte[] draw(Framebuffer target,boolean routed){
        RenderSystem.disableDepthTest();RenderSystem.disableBlend();RenderSystem.disableCull();RenderSystem.disableScissor();RenderSystem.colorMask(true,true,true,true);
        target.setClearColor(0,0,0,0);target.clear(false);target.beginWrite(true);
        try(var state=new HostedStencilState()){
            HostedStencilState.prepare();
            if(routed){try(var scope=HostedRenderTarget.begin(target)){ActualScopeCalls.enable();}}
            else ActualScopeCalls.enable();
            check(glGetInteger(GL_DRAW_FRAMEBUFFER_BINDING)==target.fbo,"helper preserves active framebuffer");
            glBindVertexArray(vao);glUseProgram(program);glUniform2f(glGetUniformLocation(program,"size"),target.textureWidth,target.textureHeight);
            RenderSystem.colorMask(false,false,false,false);RenderSystem.stencilMask(255);RenderSystem.stencilFunc(GL_ALWAYS,1,255);RenderSystem.stencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
            glUniform1i(glGetUniformLocation(program,"maskPass"),1);glDrawArrays(GL_TRIANGLES,0,3);
            RenderSystem.colorMask(true,true,true,true);RenderSystem.stencilMask(0);RenderSystem.stencilFunc(GL_EQUAL,1,255);RenderSystem.stencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
            glUniform1i(glGetUniformLocation(program,"maskPass"),0);glDrawArrays(GL_TRIANGLES,0,3);
            var bytes=org.lwjgl.BufferUtils.createByteBuffer(target.textureWidth*target.textureHeight*4);glReadPixels(0,0,target.textureWidth,target.textureHeight,GL_RGBA,GL_UNSIGNED_BYTE,bytes);byte[] result=new byte[bytes.remaining()];bytes.get(result);return result;
        }
    }
    static void pixels(byte[] pixels,int w,int h,boolean clipped){
        int visible=0;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){
            double dx=(x+.5)/w-.67,dy=(y+.5)/h-.44;boolean inside=Math.hypot(dx,dy)<.19;int alpha=pixels[(y*w+x)*4+3]&255;
            if(alpha!=0)visible++;
            check(alpha==((!clipped||inside)?255:0),"ocular clipping "+w+"x"+h+" "+x+","+y+" alpha="+alpha);
        }
        check(visible>0,"nonempty rendered scope");
    }
    public static void main(String[] args)throws Exception{
        check(GLFW.glfwInit(),"GLFW init");GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE,GLFW.GLFW_FALSE);GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR,3);GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR,3);GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE,GLFW.GLFW_OPENGL_CORE_PROFILE);
        long window=GLFW.glfwCreateWindow(160,100,"GoldCraft offscreen target regression",0,0);check(window!=0,"hidden context");GLFW.glfwMakeContextCurrent(window);GL.createCapabilities();RenderSystem.initRenderThread();
        // Load the actual NeoForge default client spec without a game/FML session.
        var config=com.electronwill.nightconfig.core.CommentedConfig.inMemory();
        var specField=net.neoforged.neoforge.common.NeoForgeConfig.class.getDeclaredField("clientSpec");specField.setAccessible(true);
        var spec=(net.neoforged.neoforge.common.ModConfigSpec)specField.get(null);spec.correct(config);
        var constructor=Class.forName("net.neoforged.fml.config.LoadedConfig").getDeclaredConstructors()[0];constructor.setAccessible(true);
        spec.acceptConfig((net.neoforged.fml.config.IConfigSpec.ILoadedConfig)constructor.newInstance(config,null,null));
        glPixelStorei(GL_PACK_ALIGNMENT,1);
        initShader();var client=MinecraftClient.getInstance();var original=new SimpleFramebuffer(48,48,true,false);original.enableStencil();client.framebuffer=original;
        check(client.getFramebuffer()==original,"standalone original target");
        var legacy=new SimpleFramebuffer(80,48,true,false);pixels(draw(legacy,false),80,48,false);legacy.delete();
        for(int[] dimensions:new int[][]{{80,48},{48,80},{127,71},{96,54}}){
            System.out.println("GPU scope target "+dimensions[0]+"x"+dimensions[1]);
            var target=HostedRenderTarget.create(dimensions[0],dimensions[1]);check(target.isStencilEnabled(),"host stencil capability");
            hostileStencil();var before=stencilState();pixels(draw(target,true),dimensions[0],dimensions[1],true);check(Arrays.equals(before,stencilState()),"both stencil faces restored");
            try(var scope=HostedRenderTarget.begin(target)){
                check(client.getFramebuffer()==target,"actual injected main-target getter");
                var threadFailure=new java.util.concurrent.atomic.AtomicReference<Throwable>();
                Thread t=new Thread(()->{try{if(client.getFramebuffer()!=original)throw new AssertionError("wrong off-thread target");}catch(Throwable failure){threadFailure.set(failure);}});t.start();t.join();
                check(threadFailure.get()==null,"render target isolated from non-render thread");
                try(var nested=HostedRenderTarget.begin(original)){check(client.getFramebuffer()==original,"nested target");}
                check(client.getFramebuffer()==target,"nested scope restores parent");
                try{try(var nested=HostedRenderTarget.begin(original)){throw new IllegalStateException("test");}}catch(IllegalStateException expected){}
                check(client.getFramebuffer()==target,"exception restores parent");
            }
            check(client.getFramebuffer()==original,"scope restores standalone");
            try{try(var scope=HostedRenderTarget.begin(target)){throw new IllegalStateException("outer test");}}catch(IllegalStateException expected){}
            check(client.getFramebuffer()==original,"exception restores standalone");
            target.beginWrite(true);RenderSystem.stencilMask(255);RenderSystem.clearStencil(231);RenderSystem.clear(GL_STENCIL_BUFFER_BIT,false);RenderSystem.stencilMask(0);
            HostedStencilState.prepare();var stencil=org.lwjgl.BufferUtils.createByteBuffer(dimensions[0]*dimensions[1]);glReadPixels(0,0,dimensions[0],dimensions[1],GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,stencil);
            while(stencil.hasRemaining())check(stencil.get()==0,"fresh frame clears stale stencil despite incoming write mask");
            target.resize(dimensions[1],dimensions[0],false);check(target.isStencilEnabled(),"resize retains stencil");pixels(draw(target,true),dimensions[1],dimensions[0],true);target.delete();
        }
        check(glGetError()==GL_NO_ERROR,"GL error");original.delete();glDeleteVertexArrays(vao);glDeleteProgram(program);GLFW.glfwDestroyWindow(window);GLFW.glfwTerminate();
        System.out.println("PASS "+checks+" actual framebuffer/TACZ helper/GPU clipping/routing/state/resize checks; no gameplay claim");
    }
}
