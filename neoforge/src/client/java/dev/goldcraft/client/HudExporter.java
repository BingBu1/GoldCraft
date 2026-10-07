package dev.goldcraft.client;

import com.google.gson.JsonObject;
import com.mojang.blaze3d.platform.GlStateManager;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.blaze3d.systems.VertexSorter;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.HudPixels;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.bridge.FramePacer;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.client.mixin.GameRendererAccessor;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.SimpleFramebuffer;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.Screen;
import net.minecraft.client.render.BufferRenderer;
import net.minecraft.client.render.DiffuseLighting;
import net.minecraft.client.render.RenderTickCounter;
import org.joml.Matrix4f;
import org.joml.Quaternionf;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;
import org.lwjgl.opengl.GL13;
import org.lwjgl.opengl.GL14;
import org.lwjgl.opengl.GL15;
import org.lwjgl.opengl.GL20;
import org.lwjgl.opengl.GL21;
import org.lwjgl.opengl.GL30;
import org.lwjgl.opengl.GL32;

/** Renders vanilla hands and UI into one transparent image at the host's physical resolution. */
public final class HudExporter implements AutoCloseable {
    private static boolean rendering;
    private final BridgeLink link;
    private final FramePacer pacing=new FramePacer(60);
    private final Staging[] staging={new Staging(),new Staging(),new Staging()};
    private SimpleFramebuffer target;
    private long viewportEpoch,viewportId,revision,menuGeneration,lastRender,sent,bytes,skipped;
    private long handFrames,lastSent;
    private double frameIntervalMs,readbackAgeMs;
    private float tickDelta;
    private boolean handVisible;
    private int viewWidth,viewHeight,requestedScale,width,height;
    private Screen screen;
    private String lastError="";
    private int glError;
    private boolean enabled;
    private static final class Staging {
        int buffer,capacity;long fence,revision,epoch,viewport,menu;
        long produced;
        int viewWidth,viewHeight,width,height,flags,life,slot,food,armor,level;
        float health,experience;
    }
    public HudExporter(BridgeLink link){this.link=link;HostUi.configure(this);}
    public static boolean rendering(){return rendering;}
    public long menuId(){return screen==null?0:menuGeneration;}
    public Screen screen(){return screen;}
    public int width(){return width;}
    public int height(){return height;}
    public void viewport(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload);long epoch=r.i64(),id=r.i64();int w=r.i32(),h=r.i32(),scale=r.i32(),on=r.i32();r.finish();
        if(id==0||w<320||h<240||w>4096||h>4096||scale<0||scale>8||on<0||on>1)throw new IllegalArgumentException("HUD viewport bounds");
        if(viewportEpoch!=epoch||viewportId!=id){clearPending();lastRender=0;pacing.reset();}
        viewportEpoch=epoch;viewportId=id;viewWidth=w;viewHeight=h;requestedScale=scale;enabled=on!=0;
    }
    public void reset() {
        clearPending();lastRender=viewportEpoch=viewportId=0;
        pacing.reset();
        if(screen!=null) {
            var client=MinecraftClient.getInstance();
            if(client.currentScreen==screen)screen.resize(client,client.getWindow().getScaledWidth(),client.getWindow().getScaledHeight());
        }
        screen=null;menuGeneration++;HostUi.reset();
    }
    private void clearPending(){for(var s:staging)if(s.fence!=0){GL32.glDeleteSync(s.fence);s.fence=0;}}
    @Override public void close(){reset();if(target!=null){target.delete();target=null;}for(var s:staging)if(s.buffer!=0){GL15.glDeleteBuffers(s.buffer);s.buffer=0;}}
    public void frame(MinecraftClient client) {
        if(!enabled||!link.connected()||!HostInput.hosted()||client.player==null||client.world==null
            ||viewportEpoch!=GoldCraftClient.HOST.epoch()||viewportId==0||client.getOverlay()!=null)return;
        long began=Performance.begin();
        try {
            shipReady();
            if(!pacing.ready(System.nanoTime()))return;
            Staging slot=null;for(var s:staging)if(s.fence==0){slot=s;break;}
            if(slot==null){skipped++;return;}
            lastRender=System.nanoTime();
            render(client,slot);
            lastError="";
        }catch(RuntimeException error){
            if(!error.toString().equals(lastError)){lastError=error.toString();GoldCraft.LOGGER.warn("HUD export failed",error);}
        }finally{Performance.end("hudExport",began);}
    }
    private void render(MinecraftClient client,Staging slot) {
        var window=client.getWindow();
        int oldWidth=window.getFramebufferWidth(),oldHeight=window.getFramebufferHeight();double oldScale=window.getScaleFactor();
        try(var saved=new GlState()) {
            window.setFramebufferWidth(viewWidth);window.setFramebufferHeight(viewHeight);
            int scale=window.calculateScaleFactor(requestedScale==0?client.options.getGuiScale().getValue():requestedScale,client.forcesUnicodeFont());
            while((viewWidth+scale-1)/scale>1024||(viewHeight+scale-1)/scale>1024)scale++;
            width=(viewWidth+scale-1)/scale;height=(viewHeight+scale-1)/scale;
            // Keep the full raster resolution: glyphs and item models lose detail if a GUI-sized image is enlarged.
            window.setScaleFactor(scale);
            if(target==null||target.textureWidth!=viewWidth||target.textureHeight!=viewHeight) {
                if(target!=null)target.delete();target=new SimpleFramebuffer(viewWidth,viewHeight,true,MinecraftClient.IS_SYSTEM_MAC);
                target.setClearColor(0,0,0,0);
            }
            if(screen!=client.currentScreen){screen=client.currentScreen;menuGeneration++;HostUi.reset();}
            if(screen!=null&&(screen.width!=width||screen.height!=height))screen.resize(client,width,height);
            RenderSystem.disableScissor();RenderSystem.colorMask(true,true,true,true);RenderSystem.depthMask(true);
            target.clear(MinecraftClient.IS_SYSTEM_MAC);target.beginWrite(true);
            var modelView=RenderSystem.getModelViewStack();modelView.pushMatrix();
            try {
                rendering=true;
                tickDelta=client.getRenderTickCounter().getTickDelta(false);
                modelView.identity();RenderSystem.applyModelViewMatrix();
                RenderSystem.setShaderColor(1,1,1,1);RenderSystem.enableDepthTest();RenderSystem.depthFunc(GL11.GL_LEQUAL);
                RenderSystem.enableCull();RenderSystem.disableBlend();DiffuseLighting.enableForLevel();
                var camera=client.gameRenderer.getCamera();
                client.getEntityRenderDispatcher().configure(client.world,camera,client.targetedEntity);
                client.gameRenderer.getLightmapTextureManager().update(tickDelta);
                // Preserve vanilla equip/swing, skin, map, bow, shield, eating and overlay rendering.
                if(HostInput.controlling()&&client.options.getPerspective().isFirstPerson())((GameRendererAccessor)client.gameRenderer).goldcraft$renderHand(camera,tickDelta,
                    new Matrix4f().rotation(camera.getRotation().conjugate(new Quaternionf())));
                handVisible=HostInput.controlling()&&client.options.getPerspective().isFirstPerson()&&!client.options.hudHidden
                    &&!client.player.isSleeping()&&!client.player.isSpectator();
                if(handVisible)handFrames++;
                RenderSystem.clear(GL11.GL_DEPTH_BUFFER_BIT,MinecraftClient.IS_SYSTEM_MAC);
                RenderSystem.setProjectionMatrix(new Matrix4f().setOrtho(0,(float)viewWidth/scale,(float)viewHeight/scale,0,1000,21000),VertexSorter.BY_Z);
                modelView.translation(0,0,-11000);RenderSystem.applyModelViewMatrix();
                RenderSystem.setShaderColor(1,1,1,1);RenderSystem.disableCull();RenderSystem.enableBlend();
                RenderSystem.defaultBlendFunc();DiffuseLighting.enableGuiDepthLighting();
                var context=new DrawContext(client,client.getBufferBuilders().getEntityVertexConsumers());
                if(HostInput.controlling())client.inGameHud.render(context,client.getRenderTickCounter());
                else if(!client.options.hudHidden&&!client.inGameHud.getChatHud().isChatFocused()) {
                    // Preserve vanilla message formatting, death notices and fading.
                    // Native CS still draws its own weapon, health and ammunition HUD.
                    client.inGameHud.getChatHud().render(context,client.inGameHud.getTicks(),
                        (int)HostUi.mouseX(),(int)HostUi.mouseY(),false);
                }
                context.draw();
                if(screen!=null) {
                    RenderSystem.clear(GL11.GL_DEPTH_BUFFER_BIT,MinecraftClient.IS_SYSTEM_MAC);
                    screen.renderWithTooltip(context,(int)HostUi.mouseX(),(int)HostUi.mouseY(),tickDelta);
                    context.draw();
                }
            }finally{rendering=false;modelView.popMatrix();RenderSystem.applyModelViewMatrix();}
            slot.epoch=viewportEpoch;slot.viewport=viewportId;slot.revision=++revision;slot.menu=menuId();
            slot.viewWidth=viewWidth;slot.viewHeight=viewHeight;slot.width=width;slot.height=height;slot.flags=2|(HostInput.controlling()?4:8)|(screen!=null?1:0);
            slot.life=HostInput.life();slot.produced=System.nanoTime();
            var player=client.player;slot.slot=player.getInventory().selectedSlot;slot.health=player.getHealth();
            slot.food=player.getHungerManager().getFoodLevel();slot.armor=player.getArmor();slot.level=player.experienceLevel;slot.experience=player.experienceProgress;
            enqueue(slot);glError=GL11.glGetError();
        }finally{rendering=false;window.setFramebufferWidth(oldWidth);window.setFramebufferHeight(oldHeight);window.setScaleFactor(oldScale);}
    }
    private void enqueue(Staging slot) {
        int old=GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING),alignment=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT);
        int row=GL11.glGetInteger(GL11.GL_PACK_ROW_LENGTH),rows=GL11.glGetInteger(GL11.GL_PACK_SKIP_ROWS),pixels=GL11.glGetInteger(GL11.GL_PACK_SKIP_PIXELS);
        try {
            if(slot.buffer==0)slot.buffer=GL15.glGenBuffers();GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,slot.buffer);
            int size=slot.viewWidth*slot.viewHeight*4;
            if(slot.capacity!=size){GL15.glBufferData(GL21.GL_PIXEL_PACK_BUFFER,size,GL15.GL_STREAM_READ);slot.capacity=size;}
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1);GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,0);
            GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,0);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,0);
            GL11.glReadPixels(0,0,slot.viewWidth,slot.viewHeight,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,0L);
            slot.fence=GL32.glFenceSync(GL32.GL_SYNC_GPU_COMMANDS_COMPLETE,0);GL11.glFlush();
        }finally{
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,old);GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,alignment);
            GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,row);GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,rows);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,pixels);
        }
    }
    private void shipReady() {
        Staging newest=null;
        for(var s:staging)if(s.fence!=0) {
            int status=GL32.glClientWaitSync(s.fence,0,0);
            if(status==GL32.GL_WAIT_FAILED)throw new IllegalStateException("HUD GPU fence failed");
            if((status==GL32.GL_ALREADY_SIGNALED||status==GL32.GL_CONDITION_SATISFIED)&&(newest==null||s.revision>newest.revision))newest=s;
        }
        if(newest==null)return;
        int old=GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        try {
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,newest.buffer);
            var mapped=GL30.glMapBufferRange(GL21.GL_PIXEL_PACK_BUFFER,0,newest.capacity,GL30.GL_MAP_READ_BIT);
            if(mapped==null)throw new IllegalStateException("HUD readback mapping failed");
            byte[] image;
            try{image=HudPixels.encode(mapped,newest.viewWidth,newest.viewHeight);}finally{GL15.glUnmapBuffer(GL21.GL_PIXEL_PACK_BUFFER);}
            var s=newest;
            if(s.life!=HostInput.life()||s.epoch!=GoldCraftClient.HOST.epoch()||s.viewport!=viewportId)return;
            byte[] payload=new Wire.Writer().i64(s.epoch).i64(s.revision).i64(s.viewport).i64(s.menu)
                .i32(s.viewWidth).i32(s.viewHeight).i32(s.width).i32(s.height).i32(s.flags).i32(s.life).i32(s.slot)
                .f32(s.health).i32(s.food).i32(s.armor).i32(s.level).f32(s.experience).bytes(image).toByteArray();
            if(link.send(Wire.HUD_FRAME,payload)){
                long now=System.nanoTime();if(lastSent!=0)frameIntervalMs=(now-lastSent)/1_000_000.0;
                readbackAgeMs=(now-s.produced)/1_000_000.0;lastSent=now;sent++;bytes+=payload.length;
            }
        }finally{
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,old);
            for(var s:staging)if(s.fence!=0&&s.revision<=newest.revision){GL32.glDeleteSync(s.fence);s.fence=0;}
        }
    }
    public void diagnostics(JsonObject data){
        data.addProperty("hudFrames",sent);data.addProperty("hudBytes",bytes);data.addProperty("hudSkipped",skipped);
        data.addProperty("hudWidth",width);data.addProperty("hudHeight",height);data.addProperty("hudGlError",glError);
        data.addProperty("hudTextureWidth",viewWidth);data.addProperty("hudTextureHeight",viewHeight);
        data.addProperty("hudError",lastError);data.addProperty("hudMenuId",menuId());HostUi.diagnostics(data);
        data.addProperty("handFrames",handFrames);data.addProperty("handVisible",handVisible);
        data.addProperty("presentationTickDelta",tickDelta);data.addProperty("presentationIntervalMs",frameIntervalMs);
        data.addProperty("presentationReadbackMs",readbackAgeMs);data.addProperty("minecraftFps",MinecraftClient.getInstance().getCurrentFps());
    }
    /** Restore both the driver state and Minecraft's cached RenderSystem state. */
    private static final class GlState implements AutoCloseable {
        final int read=GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING),draw=GL11.glGetInteger(GL30.GL_DRAW_FRAMEBUFFER_BINDING);
        final int program=GL11.glGetInteger(GL20.GL_CURRENT_PROGRAM),vao=GL11.glGetInteger(GL30.GL_VERTEX_ARRAY_BINDING),buffer=GL11.glGetInteger(GL15.GL_ARRAY_BUFFER_BINDING);
        final int active=GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE),depthFunc=GL11.glGetInteger(GL11.GL_DEPTH_FUNC);
        final boolean depth=GL11.glIsEnabled(GL11.GL_DEPTH_TEST),depthMask=GL11.glGetBoolean(GL11.GL_DEPTH_WRITEMASK);
        final boolean cull=GL11.glIsEnabled(GL11.GL_CULL_FACE),blend=GL11.glIsEnabled(GL11.GL_BLEND),scissor=GL11.glIsEnabled(GL11.GL_SCISSOR_TEST);
        final int src=GL11.glGetInteger(GL14.GL_BLEND_SRC_RGB),dst=GL11.glGetInteger(GL14.GL_BLEND_DST_RGB),srcA=GL11.glGetInteger(GL14.GL_BLEND_SRC_ALPHA),dstA=GL11.glGetInteger(GL14.GL_BLEND_DST_ALPHA);
        final int[] viewport=new int[4],scissorBox=new int[4],textures=new int[3],shaderTextures=new int[3];
        final float[] clear=new float[4],color=RenderSystem.getShaderColor().clone();
        final Matrix4f projection=new Matrix4f(RenderSystem.getProjectionMatrix());
        final VertexSorter sorter=RenderSystem.getVertexSorting();
        final net.minecraft.client.gl.ShaderProgram shader=RenderSystem.getShader();
        GlState(){
            GL11.glGetIntegerv(GL11.GL_VIEWPORT,viewport);GL11.glGetIntegerv(GL11.GL_SCISSOR_BOX,scissorBox);GL11.glGetFloatv(GL11.GL_COLOR_CLEAR_VALUE,clear);
            for(int i=0;i<3;i++){RenderSystem.activeTexture(GL13.GL_TEXTURE0+i);textures[i]=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);shaderTextures[i]=RenderSystem.getShaderTexture(i);}
            RenderSystem.activeTexture(active);
        }
        public void close(){
            BufferRenderer.reset();GlStateManager._glBindVertexArray(vao);GL15.glBindBuffer(GL15.GL_ARRAY_BUFFER,buffer);
            GlStateManager._glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,read);GlStateManager._glBindFramebuffer(GL30.GL_DRAW_FRAMEBUFFER,draw);
            RenderSystem.viewport(viewport[0],viewport[1],viewport[2],viewport[3]);RenderSystem.setProjectionMatrix(projection,sorter);
            RenderSystem.setShader(()->shader);GlStateManager._glUseProgram(program);RenderSystem.setShaderColor(color[0],color[1],color[2],color[3]);
            for(int i=0;i<3;i++){RenderSystem.activeTexture(GL13.GL_TEXTURE0+i);RenderSystem.bindTexture(textures[i]);RenderSystem.setShaderTexture(i,shaderTextures[i]);}RenderSystem.activeTexture(active);
            if(depth)RenderSystem.enableDepthTest();else RenderSystem.disableDepthTest();RenderSystem.depthMask(depthMask);RenderSystem.depthFunc(depthFunc);
            if(cull)RenderSystem.enableCull();else RenderSystem.disableCull();if(blend)RenderSystem.enableBlend();else RenderSystem.disableBlend();
            RenderSystem.blendFuncSeparate(src,dst,srcA,dstA);
            if(scissor)RenderSystem.enableScissor(scissorBox[0],scissorBox[1],scissorBox[2],scissorBox[3]);else RenderSystem.disableScissor();
            RenderSystem.clearColor(clear[0],clear[1],clear[2],clear[3]);
        }
    }
}
