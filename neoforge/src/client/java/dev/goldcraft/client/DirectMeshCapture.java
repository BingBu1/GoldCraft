package dev.goldcraft.client;

import com.mojang.blaze3d.platform.GlStateManager;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.systems.VertexSorter;
import dev.goldcraft.bridge.ParticleSnapshot;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import net.minecraft.client.gl.ShaderProgram;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.BuiltBuffer;
import net.minecraft.client.render.VertexConsumerProvider;
import net.minecraft.client.render.VertexFormatElement;
import net.minecraft.util.math.Vec3d;
import net.neoforged.neoforge.client.GlStateBackup;
import org.joml.Matrix4fStack;
import org.joml.Matrix4f;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL14;
import org.lwjgl.opengl.GL20;

/** Only active around hosted world export, never around normal MC or HUD rendering. */
public final class DirectMeshCapture implements AutoCloseable {
    record Draw(int glTexture,ParticleSnapshot.Material material,byte[] vertices) {}
    private static DirectMeshCapture active;
    private final GlStateBackup saved=new GlStateBackup(),current=new GlStateBackup();
    private final ShaderProgram shader=RenderSystem.getShader();
    private final float[] color=RenderSystem.getShaderColor().clone();
    private final int activeTexture=GlStateManager._getActiveTexture();
    private final int[] textures=new int[3],shaderTextures=new int[3];
    private final int equationRgb=GL11.glGetInteger(GL20.GL_BLEND_EQUATION_RGB),equationAlpha=GL11.glGetInteger(GL20.GL_BLEND_EQUATION_ALPHA);
    private final boolean stencil=GL11.glIsEnabled(GL11.GL_STENCIL_TEST);
    private final EffectBuffers buffers;
    private final VertexConsumerProvider.Immediate provider;
    private ModelViewScope modelView;
    private final Matrix4f projection=new Matrix4f(RenderSystem.getProjectionMatrix());
    private final Matrix4f textureMatrix=new Matrix4f(RenderSystem.getTextureMatrix());
    private final VertexSorter sorting=RenderSystem.getVertexSorting();
    private final Vec3d origin;
    private final boolean redirectBuffers;
    private final List<Draw> draws=new ArrayList<>();
    private final Map<String,Integer> unsupported=new TreeMap<>();
    private final Map<String,Integer> shaders=new TreeMap<>();
    private final Map<String,Integer> sources=new TreeMap<>();
    private String source="world";
    private int equation=GL14.GL_FUNC_ADD,vertices;
    private boolean finished,closed,failed;
    private boolean stateSaved;
    private int capturedTextures;
    private int viewOffsetLayers;

    static DirectMeshCapture begin(Vec3d origin,boolean redirectBuffers){
        if(active!=null)throw new IllegalStateException("Nested world draw capture");
        return new DirectMeshCapture(origin,redirectBuffers);
    }
    private DirectMeshCapture(Vec3d origin,boolean redirectBuffers) {
        this.origin=origin;this.redirectBuffers=redirectBuffers;
        buffers=EffectBuffers.acquire(MinecraftClient.getInstance().getBufferBuilders().getEntityVertexConsumers());
        try {
        provider=buffers.provider();
        GlStateManager._backupGlState(saved);stateSaved=true;
        for(int i=0;i<3;i++) {
            RenderSystem.activeTexture(GL13.GL_TEXTURE0+i);
            textures[i]=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);shaderTextures[i]=RenderSystem.getShaderTexture(i);
            capturedTextures++;
        }
        RenderSystem.activeTexture(GL13.GL_TEXTURE0);
        modelView=ModelViewScope.begin(new Matrix4fStack(16));
        RenderSystem.getModelViewStack().identity();RenderSystem.applyModelViewMatrix();
        var camera=MinecraftClient.getInstance().gameRenderer.getCamera().getPos();
        RenderSystem.setProjectionMatrix(projection,VertexSorter.byDistance((float)(camera.x-origin.x),
            (float)(camera.y-origin.y),(float)(camera.z-origin.z)));
        RenderSystem.enableDepthTest();RenderSystem.depthFunc(GL11.GL_LEQUAL);RenderSystem.depthMask(true);
        RenderSystem.disableCull();RenderSystem.disableBlend();RenderSystem.defaultBlendFunc();
        RenderSystem.colorMask(true,true,true,true);RenderSystem.disableScissor();RenderSystem.disablePolygonOffset();
        RenderSystem.disableColorLogicOp();
        RenderSystem.setShaderColor(1,1,1,1);GlStateManager._blendEquation(GL14.GL_FUNC_ADD);
        GL11.glDisable(GL11.GL_STENCIL_TEST);
        active=this;
        }catch(RuntimeException|Error failure) {
            failed=true;closed=true;
            var cleanup=new CaptureCleanup(failure);
            cleanup.run(()->buffers.release(false));
            restore(cleanup);
            throw failure;
        }
    }
    public static boolean capturing(){return active!=null;}
    /** Keep layer offsets in camera space, shared with the base mesh on the host. */
    public static boolean viewOffsetLayer(boolean begin){
        if(active==null)return false;
        if(begin)active.viewOffsetLayers++;
        else if(active.viewOffsetLayers>0)active.viewOffsetLayers--;
        else throw new IllegalStateException("Unbalanced view-offset layer");
        return true;
    }
    public static VertexConsumerProvider.Immediate redirectedBuffers(){return active!=null&&active.redirectBuffers?active.provider:null;}
    static VertexConsumerProvider.Immediate entityBuffers(){return active==null?null:active.provider;}
    public static void blendEquation(int value){if(active!=null)active.equation=value;}

    /** Cancelling GPU submission transfers ownership of both native buffers to this scope. */
    public static boolean capture(BuiltBuffer buffer,boolean globalProgram) {
        if(active==null||!RenderSystem.isOnRenderThread())return false;
        try(buffer) {
            // Manual uploads use an already-bound program, which need not be
            // RenderSystem's global shader. Never infer its semantics from that stale global.
            if(!globalProgram){active.reject("manual_shader");return true;}
            active.copy(buffer);
        }catch(RuntimeException|Error failure) {
            active.failed=true;throw failure;
        }
        return true;
    }
    private void reject(String reason){if(unsupported.size()<64||unsupported.containsKey(reason))unsupported.merge(reason,1,Integer::sum);}
    private void copy(BuiltBuffer buffer) {
        var program=RenderSystem.getShader();
        var policy=program==null?null:EffectShader.of(program.getName());
        if(VixPostCapture.capturing()&&program!=null&&program.getName().replace("minecraft:","").equals("position_color_lightmap"))
            policy=new EffectShader(false,false,0,16);
        if(policy==null){reject("shader:"+(program==null?"null":program.getName()));return;}
        if(!projection.equals(RenderSystem.getProjectionMatrix())){reject("changed_projection");return;}
        GlStateManager._backupGlState(current);
        if(current.scissorEnabled||current.polyOffsetFillEnabled||current.polyOffsetLineEnabled||current.colorLogicEnabled){reject("unsupported_draw_state");return;}
        var parameters=buffer.getDrawParameters();
        EffectGeometry.Mode mode;
        try{mode=EffectGeometry.Mode.valueOf(parameters.mode().name());}
        catch(IllegalArgumentException unsupportedMode){reject("topology:"+parameters.mode());return;}
        var format=parameters.format();
        var layout=new EffectGeometry.Layout(format.getVertexSizeByte(),format.getOffset(VertexFormatElement.POSITION),
            format.getOffset(VertexFormatElement.COLOR),format.getOffset(VertexFormatElement.UV_0),format.getOffset(VertexFormatElement.UV_2));
        int texture=policy.texture()?RenderSystem.getShaderTexture(0):ParticleSnapshot.WHITE_TEXTURE;
        if(policy.texture()&&(texture<=0||layout.uv()<0)){reject("missing_texture");return;}
        if(draws.size()>=ParticleSnapshot.MAX_BATCHES)throw new IllegalArgumentException("Effect batch budget");
        boolean glint=EffectGeometry.usesTextureMatrix(program.getName());
        byte[] mesh;
        try {
            var input=VixPostCapture.maskVertices(buffer.getBuffer(),parameters.vertexCount(),layout,program.getName());
            mesh=EffectGeometry.convert(input,parameters.vertexCount(),layout,mode,buffer.getSortedBuffer(),
                parameters.indexType().size,parameters.indexCount(),RenderSystem.getModelViewMatrix(),
                glint?RenderSystem.getTextureMatrix():null,origin.x,origin.y,origin.z,
                policy.light()&&!VixPostCapture.capturing(),ParticleSnapshot.MAX_VERTICES-vertices);
        }catch(IllegalArgumentException failure) {
            // Detail only on failure: no per-draw diagnostic string/allocation.
            throw new IllegalArgumentException("Effect draw source="+source+" shader="+program.getName()
                +" mode="+mode+" layout="+layout+" count="+parameters.vertexCount()
                +" origin="+origin+" modelView="+RenderSystem.getModelViewMatrix()
                +": "+failure.getMessage(),failure);
        }
        if(mesh.length==0)return;
        var material=material(current,equation,policy,RenderSystem.getShaderColor(),glint,glint?RenderSystem.getShaderGlintAlpha():1,viewOffsetLayers!=0);
        draws.add(new Draw(texture,material,mesh));vertices+=mesh.length/24;
        shaders.merge(program.getName(),1,Integer::sum);
        if(sources.size()<128||sources.containsKey(source))sources.merge(source,1,Integer::sum);
    }
    static ParticleSnapshot.Material material(GlStateBackup state,int equation,EffectShader policy,float[] color,boolean glint,float glintAlpha) {
        return material(state,equation,policy,color,glint,glintAlpha,false);
    }
    static ParticleSnapshot.Material material(GlStateBackup state,int equation,EffectShader policy,float[] color,boolean glint,float glintAlpha,boolean viewOffset) {
        int flags=(state.depthMask?1:0)|(state.blendEnabled?2:0)|(state.depthEnabled?4:0)|(state.cullEnabled?8:0)|policy.flags()|(glint?128:0)|(viewOffset?256:0);
        int mask=(state.colorMaskRed?1:0)|(state.colorMaskGreen?2:0)|(state.colorMaskBlue?4:0)|(state.colorMaskAlpha?8:0);
        // Vanilla GlintAlpha scales RGB after the alpha test, leaving alpha intact.
        float rgb=glint?glintAlpha:1;
        return new ParticleSnapshot.Material(flags,state.blendSrcRgb,state.blendDestRgb,state.blendSrcAlpha,state.blendDestAlpha,
            equation,equation,state.depthFunc,mask,policy.cutoff(),color[0]*rgb,color[1]*rgb,color[2]*rgb,color[3]);
    }
    void finish(){
        if(failed)throw new IllegalStateException("Failed effect capture");
        if(!finished)try{provider.draw();finished=true;}catch(RuntimeException|Error failure){failed=true;throw failure;}
    }
    List<Draw> draws(){if(!finished)throw new IllegalStateException("Unflushed effect capture");return List.copyOf(draws);}
    Map<String,Integer> unsupported(){return Map.copyOf(unsupported);}
    Map<String,Integer> shaders(){return Map.copyOf(shaders);}
    Map<String,Integer> sources(){return Map.copyOf(sources);}
    void source(String value){source=value;}
    int vertices(){return vertices;}
    int drawCount(){return draws.size();}
    List<Draw> takePostDraws(){provider.draw();var result=List.copyOf(draws);draws.clear();return result;}
    @Override public void close() {
        if(closed)return;
        var cleanup=new CaptureCleanup();
        if(!failed)cleanup.run(this::finish);
        closed=true;active=null;
        cleanup.run(()->buffers.release(finished&&!failed));
        restore(cleanup);
        cleanup.rethrow();
    }
    private void restore(CaptureCleanup cleanup) {
        if(modelView!=null) {cleanup.run(modelView::close);cleanup.run(RenderSystem::applyModelViewMatrix);}
        if(!stateSaved)return;
        cleanup.run(()->RenderSystem.setProjectionMatrix(projection,sorting));
        cleanup.run(()->RenderSystem.setTextureMatrix(textureMatrix));
        cleanup.run(()->GlStateManager._restoreGlState(saved));
        cleanup.run(()->GL20.glBlendEquationSeparate(equationRgb,equationAlpha));
        cleanup.run(()->{if(stencil)GL11.glEnable(GL11.GL_STENCIL_TEST);else GL11.glDisable(GL11.GL_STENCIL_TEST);});
        cleanup.run(()->RenderSystem.setShader(()->shader));
        cleanup.run(()->RenderSystem.setShaderColor(color[0],color[1],color[2],color[3]));
        for(int i=0;i<capturedTextures;i++) {
            final int slot=i;
            cleanup.run(()->{RenderSystem.activeTexture(GL13.GL_TEXTURE0+slot);RenderSystem.bindTexture(textures[slot]);});
            cleanup.run(()->RenderSystem.setShaderTexture(slot,shaderTextures[slot]));
        }
        cleanup.run(()->RenderSystem.activeTexture(activeTexture));
    }
}
