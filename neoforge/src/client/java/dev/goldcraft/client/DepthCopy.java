package dev.goldcraft.client;

import it.unimi.dsi.fastutil.ints.Int2ObjectOpenHashMap;
import java.nio.ByteBuffer;
import java.nio.IntBuffer;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL;
import static org.lwjgl.opengl.GL33C.*;

/** Depth conversion for known, single-sample Minecraft framebuffer attachments. */
public final class DepthCopy {
    private record Attachment(int texture,int width,int height,int format) {}
    private static final Int2ObjectOpenHashMap<Attachment> attachments=new Int2ObjectOpenHashMap<>();
    // LWJGL's scalar glGet helpers create a buffer view on every call.
    // This render-thread-only scratch storage keeps warm copies allocation-free.
    private static final IntBuffer integerQuery=BufferUtils.createIntBuffer(1);
    private static final ByteBuffer booleanQuery=BufferUtils.createByteBuffer(1);
    private static Resources resources;
    private static boolean copying;
    private static long conversions;
    private DepthCopy(){}
    private static int integer(int parameter){glGetIntegerv(parameter,integerQuery);return integerQuery.get(0);}
    private static int indexedInteger(int parameter,int index){glGetIntegeri_v(parameter,index,integerQuery);return integerQuery.get(0);}
    private static boolean bool(int parameter){glGetBooleanv(parameter,booleanQuery);return booleanQuery.get(0)!=0;}

    /** Called after allocation, never from the draw loop. Texture bindings are preserved. */
    public static void allocated(int fbo,int texture,int width,int height) {
        if(fbo<=0||texture<=0||width<=0||height<=0)return;
        int previous=integer(GL_TEXTURE_BINDING_2D);
        try {
            glBindTexture(GL_TEXTURE_2D,texture);
            int format=glGetTexLevelParameteri(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT);
            attachments.put(fbo,new Attachment(texture,width,height,format));
        } finally {glBindTexture(GL_TEXTURE_2D,previous);}
    }
    public static void deleted(int fbo){attachments.remove(fbo);}
    public static long conversions(){return conversions;}
    public static void close(){attachments.clear();if(resources!=null){resources.close();resources=null;}}

    /** Return false to retain the original blit, including its validation/error behavior. */
    public static boolean convert(int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1,int mask,int filter) {
        if(copying||(mask&GL_DEPTH_BUFFER_BIT)==0||(mask&~(GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT))!=0
            ||filter!=GL_NEAREST||sx0!=0||sy0!=0||dx0!=0||dy0!=0||attachments.isEmpty())return false;
        int read=integer(GL_READ_FRAMEBUFFER_BINDING),draw=integer(GL_DRAW_FRAMEBUFFER_BINDING);
        Attachment source=attachments.get(read),destination=attachments.get(draw);
        if(source==null||destination==null||source.format==destination.format||source.texture==destination.texture
            ||sx1!=source.width||sy1!=source.height||dx1!=destination.width||dy1!=destination.height
            ||!GL.getCapabilities().OpenGL33)return false;
        if(resources==null)resources=new Resources();
        copying=true;
        try {
            // A combined color/depth blit would otherwise reject both copies.
            if((mask&GL_COLOR_BUFFER_BIT)!=0)
                glBlitFramebuffer(sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1,GL_COLOR_BUFFER_BIT,filter);
            resources.copy(source,destination);conversions++;return true;
        } finally {copying=false;}
    }

    private static final class Resources implements AutoCloseable {
        private static final int[] DISABLED={GL_STENCIL_TEST,GL_CULL_FACE,
            GL_POLYGON_OFFSET_FILL,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK};
        private final int program,vao,sampler,sizeUniform;
        private final int[] viewport=new int[4],polygon=new int[2],drawBuffers;
        private final boolean[] enabled=new boolean[DISABLED.length],clipEnabled;
        Resources() {
            int vertex=shader(GL_VERTEX_SHADER,"""
                #version 330 core
                void main() {
                    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
                    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
                }
                """);
            int fragment=0,p=0;
            try {
                fragment=shader(GL_FRAGMENT_SHADER,"""
                    #version 330 core
                    uniform sampler2D sourceDepth;
                    uniform vec4 sizes;
                    void main() {
                        ivec2 p=ivec2(gl_FragCoord.xy*sizes.xy/sizes.zw);
                        gl_FragDepth=texelFetch(sourceDepth,p,0).r;
                    }
                    """);
                p=glCreateProgram();glAttachShader(p,vertex);glAttachShader(p,fragment);glLinkProgram(p);
                if(glGetProgrami(p,GL_LINK_STATUS)==GL_FALSE)throw new IllegalStateException("Depth copy link: "+glGetProgramInfoLog(p));
            } catch(RuntimeException failure){if(p!=0)glDeleteProgram(p);throw failure;}
            finally {glDeleteShader(vertex);if(fragment!=0)glDeleteShader(fragment);}
            program=p;sizeUniform=glGetUniformLocation(p,"sizes");
            vao=glGenVertexArrays();sampler=glGenSamplers();
            glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glSamplerParameteri(sampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glSamplerParameteri(sampler,GL_TEXTURE_COMPARE_MODE,GL_NONE);
            drawBuffers=new int[integer(GL_MAX_DRAW_BUFFERS)];
            clipEnabled=new boolean[integer(GL_MAX_CLIP_DISTANCES)];
        }
        private static int shader(int type,String source) {
            int shader=glCreateShader(type);glShaderSource(shader,source);glCompileShader(shader);
            if(glGetShaderi(shader,GL_COMPILE_STATUS)==GL_FALSE){String error=glGetShaderInfoLog(shader);glDeleteShader(shader);throw new IllegalStateException("Depth copy shader: "+error);}
            return shader;
        }
        void copy(Attachment source,Attachment destination) {
            int oldProgram=integer(GL_CURRENT_PROGRAM),oldVao=integer(GL_VERTEX_ARRAY_BINDING);
            int oldActive=integer(GL_ACTIVE_TEXTURE),oldDepthFunc=integer(GL_DEPTH_FUNC);
            boolean oldDepth=glIsEnabled(GL_DEPTH_TEST),oldMask=bool(GL_DEPTH_WRITEMASK);
            glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_POLYGON_MODE,polygon);
            for(int i=0;i<drawBuffers.length;i++)drawBuffers[i]=integer(GL_DRAW_BUFFER0+i);
            for(int i=0;i<DISABLED.length;i++)enabled[i]=glIsEnabled(DISABLED[i]);
            for(int i=0;i<clipEnabled.length;i++)clipEnabled[i]=glIsEnabled(GL_CLIP_DISTANCE0+i);
            glActiveTexture(GL_TEXTURE0);
            int oldTexture=integer(GL_TEXTURE_BINDING_2D),oldSampler=indexedInteger(GL_SAMPLER_BINDING,0);
            try {
                // Preserve blit's scissor/rasterizer-discard semantics and destination stencil.
                glDrawBuffer(GL_NONE);glViewport(0,0,destination.width,destination.height);
                for(int flag:DISABLED)glDisable(flag);
                for(int i=0;i<clipEnabled.length;i++)glDisable(GL_CLIP_DISTANCE0+i);
                glEnable(GL_DEPTH_TEST);glDepthFunc(GL_ALWAYS);glDepthMask(true);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
                glUseProgram(program);glBindVertexArray(vao);glBindTexture(GL_TEXTURE_2D,source.texture);glBindSampler(0,sampler);
                glUniform4f(sizeUniform,source.width,source.height,destination.width,destination.height);
                glDrawArrays(GL_TRIANGLES,0,3);
            } finally {
                glBindTexture(GL_TEXTURE_2D,oldTexture);glBindSampler(0,oldSampler);glActiveTexture(oldActive);
                glBindVertexArray(oldVao);glUseProgram(oldProgram);
                glDepthFunc(oldDepthFunc);glDepthMask(oldMask);setEnabled(GL_DEPTH_TEST,oldDepth);
                glPolygonMode(GL_FRONT_AND_BACK,polygon[0]);
                for(int i=0;i<DISABLED.length;i++)setEnabled(DISABLED[i],enabled[i]);
                for(int i=0;i<clipEnabled.length;i++)setEnabled(GL_CLIP_DISTANCE0+i,clipEnabled[i]);
                glDrawBuffers(drawBuffers);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
            }
        }
        private static void setEnabled(int flag,boolean value){if(value)glEnable(flag);else glDisable(flag);}
        @Override public void close(){glDeleteSamplers(sampler);glDeleteVertexArrays(vao);glDeleteProgram(program);}
    }
}
