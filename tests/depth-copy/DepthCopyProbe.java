import dev.goldcraft.client.DepthCopy;
import java.lang.management.ManagementFactory;
import java.util.ArrayList;
import java.util.Arrays;
import org.lwjgl.glfw.GLFW;
import org.lwjgl.opengl.GL;
import static org.lwjgl.opengl.GL33C.*;

/** Real GPU contents/state; no game process, window input or synthetic FPS claim. */
public final class DepthCopyProbe {
    static int checks;
    static void check(boolean value,String label){checks++;if(!value)throw new AssertionError(label);}
    record Target(int fbo,int depth,int[] colors,int width,int height,boolean stencil) implements AutoCloseable {
        public void close(){DepthCopy.deleted(fbo);glDeleteFramebuffers(fbo);glDeleteTextures(depth);glDeleteTextures(colors);}
    }
    static Target target(boolean stencil,int w,int h,boolean source){
        int fbo=glGenFramebuffers(),depth=glGenTextures();int[] colors={glGenTextures(),glGenTextures()};
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        for(int i=0;i<2;i++){
            glBindTexture(GL_TEXTURE_2D,colors[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,0L);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,colors[i],0);
        }
        glBindTexture(GL_TEXTURE_2D,depth);
        glTexImage2D(GL_TEXTURE_2D,0,stencil?GL_DEPTH32F_STENCIL8:GL_DEPTH_COMPONENT,w,h,0,
            stencil?GL_DEPTH_STENCIL:GL_DEPTH_COMPONENT,stencil?GL_FLOAT_32_UNSIGNED_INT_24_8_REV:GL_FLOAT,0L);
        glFramebufferTexture2D(GL_FRAMEBUFFER,stencil?GL_DEPTH_STENCIL_ATTACHMENT:GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth,0);
        glDrawBuffers(new int[]{GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1});
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"complete framebuffer");
        glDisable(GL_SCISSOR_TEST);glDepthMask(true);glStencilMask(255);
        for(int i=0;i<2;i++)glColorMaski(i,true,true,true,true);
        glClearBufferfv(GL_COLOR,0,new float[]{source?.25f:.75f,.5f,.25f,1});
        glClearBufferfv(GL_COLOR,1,new float[]{.1f,.2f,.3f,1});
        if(stencil)glClearBufferfi(GL_DEPTH_STENCIL,0,source?.375f:.875f,73);
        else glClearBufferfv(GL_DEPTH,0,new float[]{source?.375f:.875f});
        if(source){glEnable(GL_SCISSOR_TEST);glScissor(0,0,w/2,h/2);glClearBufferfv(GL_DEPTH,0,new float[]{.125f});glDisable(GL_SCISSOR_TEST);}
        DepthCopy.allocated(fbo,depth,w,h);
        return new Target(fbo,depth,colors,w,h,stencil);
    }
    record Pixels(float[] depth,byte[][] colors,byte[] stencil){}
    static Pixels pixels(Target target){
        int old=glGetInteger(GL_READ_FRAMEBUFFER_BINDING);glBindFramebuffer(GL_READ_FRAMEBUFFER,target.fbo);
        int read=glGetInteger(GL_READ_BUFFER);float[] depth=new float[target.width*target.height];
        glReadPixels(0,0,target.width,target.height,GL_DEPTH_COMPONENT,GL_FLOAT,depth);
        byte[][] colors=new byte[2][target.width*target.height*4];
        for(int i=0;i<2;i++){glReadBuffer(GL_COLOR_ATTACHMENT0+i);var bytes=org.lwjgl.BufferUtils.createByteBuffer(colors[i].length);glReadPixels(0,0,target.width,target.height,GL_RGBA,GL_UNSIGNED_BYTE,bytes);bytes.get(colors[i]);}
        byte[] stencil=new byte[target.width*target.height];
        if(target.stencil){var bytes=org.lwjgl.BufferUtils.createByteBuffer(stencil.length);glReadPixels(0,0,target.width,target.height,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,bytes);bytes.get(stencil);}
        glReadBuffer(read);glBindFramebuffer(GL_READ_FRAMEBUFFER,old);return new Pixels(depth,colors,stencil);
    }
    static int[] state(){
        var values=new ArrayList<Integer>();
        for(int p:new int[]{GL_READ_FRAMEBUFFER_BINDING,GL_DRAW_FRAMEBUFFER_BINDING,GL_CURRENT_PROGRAM,GL_VERTEX_ARRAY_BINDING,GL_ACTIVE_TEXTURE,GL_DEPTH_FUNC,GL_TEXTURE_BINDING_2D})values.add(glGetInteger(p));
        for(int p:new int[]{GL_VIEWPORT,GL_SCISSOR_BOX}){int[] v=new int[4];glGetIntegerv(p,v);for(int x:v)values.add(x);}
        int[] polygon=new int[2];glGetIntegerv(GL_POLYGON_MODE,polygon);for(int x:polygon)values.add(x);
        for(int i=0;i<glGetInteger(GL_MAX_DRAW_BUFFERS);i++)values.add(glGetInteger(GL_DRAW_BUFFER0+i));
        for(int p:new int[]{GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_POLYGON_OFFSET_FILL,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK,GL_CLIP_DISTANCE0})values.add(glIsEnabled(p)?1:0);
        values.add(glGetBoolean(GL_DEPTH_WRITEMASK)?1:0);
        int active=glGetInteger(GL_ACTIVE_TEXTURE);glActiveTexture(GL_TEXTURE0);values.add(glGetInteger(GL_TEXTURE_BINDING_2D));values.add(glGetIntegeri(GL_SAMPLER_BINDING,0));glActiveTexture(active);
        return values.stream().mapToInt(Integer::intValue).toArray();
    }
    static void hostileState(Target source,Target destination,boolean scissor){
        glBindFramebuffer(GL_READ_FRAMEBUFFER,source.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination.fbo);
        for(int p:new int[]{GL_STENCIL_TEST,GL_CULL_FACE,GL_POLYGON_OFFSET_FILL,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK,GL_CLIP_DISTANCE0})glEnable(p);
        glStencilFunc(GL_NEVER,0,255);glSampleCoverage(0,false);glSampleMaski(0,0);glDepthFunc(GL_NEVER);glDepthMask(false);glDisable(GL_DEPTH_TEST);
        glViewport(3,2,7,9);glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glScissor(2,1,5,7);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        glColorMaski(0,false,true,false,true);glColorMaski(1,true,false,true,false);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,destination.colors[0]);glActiveTexture(GL_TEXTURE4);
    }
    static void cleanState(){
        for(int p:new int[]{GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_RASTERIZER_DISCARD,GL_POLYGON_OFFSET_FILL,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE,GL_SAMPLE_MASK,GL_CLIP_DISTANCE0})glDisable(p);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glDepthMask(true);glDepthFunc(GL_LEQUAL);glActiveTexture(GL_TEXTURE0);
    }
    static boolean convert(Target source,Target destination,int mask){return DepthCopy.convert(0,0,source.width,source.height,0,0,destination.width,destination.height,mask,GL_NEAREST);}
    static void copy(Target source,Target destination,int mask){if(!convert(source,destination,mask))glBlitFramebuffer(0,0,source.width,source.height,0,0,destination.width,destination.height,mask,GL_NEAREST);}
    static void scenario(boolean srcStencil,boolean dstStencil,boolean scaled,boolean scissor,boolean color,int route){
        cleanState();
        try(Target source=target(srcStencil,16,16,true);Target destination=target(dstStencil,scaled?8:16,scaled?12:16,false)){
            Pixels before=pixels(destination),src=pixels(source);hostileState(source,destination,scissor);int[] state=state();
            check(glGetError()==GL_NO_ERROR,"clean setup");
            long count=DepthCopy.conversions();
            var a=new ActualDepthCalls.Target(source.fbo,source.width,source.height);
            var b=new ActualDepthCalls.Target(destination.fbo,destination.width,destination.height);
            switch(route){
                case 0->copy(source,destination,GL_DEPTH_BUFFER_BIT|(color?GL_COLOR_BUFFER_BIT:0));
                case 1->ActualDepthCalls.copyDepthSafely(source.fbo,source.width,source.height,b);
                case 2->ActualDepthCalls.copyDepthSafely(a,destination.fbo,destination.width,destination.height);
                case 3->ActualDepthCalls.copySafely(a,b);
                default->throw new AssertionError("route");
            }
            check(glGetError()==GL_NO_ERROR,"copy GL error");check(Arrays.equals(state,state()),"driver state restored");
            check(DepthCopy.conversions()-count==(srcStencil==dstStencil?0:1),"conversion only for different formats");
            Pixels actual=pixels(destination);
            for(int y=0;y<destination.height;y++)for(int x=0;x<destination.width;x++){
                int i=y*destination.width+x;boolean inside=!scissor||(x>=2&&x<7&&y>=1&&y<8);
                int sx=(int)((x+.5)*source.width/destination.width),sy=(int)((y+.5)*source.height/destination.height);
                float expected=inside?src.depth[sy*source.width+sx]:before.depth[i];
                if(Math.abs(actual.depth[i]-expected)>=1e-6)throw new AssertionError("depth route="+route+" src="+srcStencil+" dst="+dstStencil+" scaled="+scaled+" scissor="+scissor+" color="+color+" x="+x+" y="+y+" actual="+actual.depth[i]+" expected="+expected);
                checks++;
                for(int attachment=0;attachment<2;attachment++)for(int channel=0;channel<4;channel++){
                    byte expectedColor=color&&inside?src.colors[0][(sy*source.width+sx)*4+channel]:before.colors[attachment][i*4+channel];
                    check(actual.colors[attachment][i*4+channel]==expectedColor,"color attachment preservation/copy");
                }
                check(actual.stencil[i]==before.stencil[i],"destination stencil unchanged");
            }
            check(Arrays.equals(src.depth,pixels(source).depth),"source depth unchanged");
        }finally{cleanState();}
    }
    static void negativeAndLifecycle(){
        try(Target source=target(true,16,16,true);Target destination=target(false,16,16,false)){
            glBindFramebuffer(GL_READ_FRAMEBUFFER,source.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination.fbo);
            glBlitFramebuffer(0,0,16,16,0,0,16,16,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
            check(glGetError()==GL_INVALID_OPERATION,"old path reproduces actual error 1282");
            check(pixels(destination).depth[0]==.875f,"failed old copy leaves stale depth");
            glEnable(GL_RASTERIZER_DISCARD);check(convert(source,destination,GL_DEPTH_BUFFER_BIT),"mixed-format discard still validates");
            check(pixels(destination).depth[0]==.875f&&glIsEnabled(GL_RASTERIZER_DISCARD),"rasterizer discard remains effective");glDisable(GL_RASTERIZER_DISCARD);
            check(!DepthCopy.convert(0,0,8,8,0,0,16,16,GL_DEPTH_BUFFER_BIT,GL_NEAREST),"partial rectangle remains original");
            check(!DepthCopy.convert(0,0,16,16,0,0,16,16,GL_DEPTH_BUFFER_BIT,GL_LINEAR),"invalid depth filtering remains original");
            check(!convert(source,destination,GL_COLOR_BUFFER_BIT),"color-only path remains original");
            check(!convert(source,destination,GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT),"stencil-copy path remains original");
            DepthCopy.deleted(source.fbo);check(!convert(source,destination,GL_DEPTH_BUFFER_BIT),"deleted framebuffer cannot reuse stale metadata");
            DepthCopy.allocated(source.fbo,source.depth,16,16);
            for(int i=0;i<128;i++)copy(source,destination,GL_DEPTH_BUFFER_BIT);
            var bean=(com.sun.management.ThreadMXBean)ManagementFactory.getThreadMXBean();long thread=Thread.currentThread().threadId();
            long before=bean.getThreadAllocatedBytes(thread);
            for(int i=0;i<256;i++)copy(source,destination,GL_DEPTH_BUFFER_BIT);
            long allocated=bean.getThreadAllocatedBytes(thread)-before;
            check(allocated<=1024,"warm depth conversion must not allocate per copy: "+allocated);
            check(glGetError()==GL_NO_ERROR,"warm repeated copies have no GL errors");
            System.out.println("warmCopies=256 allocatedBytes="+allocated);
        }
        DepthCopy.close();
        try(Target source=target(false,16,16,true);Target destination=target(true,16,16,false)){
            glBindFramebuffer(GL_READ_FRAMEBUFFER,source.fbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination.fbo);
            check(convert(source,destination,GL_DEPTH_BUFFER_BIT),"resource recreation after close");check(glGetError()==GL_NO_ERROR,"recreated copy GL error");
        }
    }
    public static void main(String[] args){
        check(GLFW.glfwInit(),"GLFW init");GLFW.glfwWindowHint(GLFW.GLFW_VISIBLE,GLFW.GLFW_FALSE);
        GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MAJOR,3);GLFW.glfwWindowHint(GLFW.GLFW_CONTEXT_VERSION_MINOR,3);
        GLFW.glfwWindowHint(GLFW.GLFW_OPENGL_PROFILE,GLFW.GLFW_OPENGL_CORE_PROFILE);
        long window=GLFW.glfwCreateWindow(32,32,"GoldCraft depth copy regression",0,0);check(window!=0,"hidden context");
        GLFW.glfwMakeContextCurrent(window);GL.createCapabilities();System.out.println("GPU="+glGetString(GL_RENDERER)+" GL="+glGetString(GL_VERSION));
        try {
            negativeAndLifecycle();
            int scenarios=0;
            for(boolean src:new boolean[]{false,true})for(boolean dst:new boolean[]{false,true})
                for(boolean scaled:new boolean[]{false,true})for(boolean scissor:new boolean[]{false,true})for(boolean color:new boolean[]{false,true})
                    for(int route=0;route<4;route++)if(route==0||color==(route==3)){scenario(src,dst,scaled,scissor,color,route);scenarios++;}
            check(glGetError()==GL_NO_ERROR,"final GL state");System.out.println("PASS checks="+checks+" scenarios="+scenarios+" conversions="+DepthCopy.conversions());
        }finally{DepthCopy.close();GLFW.glfwDestroyWindow(window);GLFW.glfwTerminate();}
    }
}
