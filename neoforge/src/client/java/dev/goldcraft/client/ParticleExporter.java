package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.ParticleSnapshot;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.bridge.FramePacer;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.client.mixin.EntityRenderDispatcherAccessor;
import dev.goldcraft.client.mixin.ParticleManagerAccessor;
import dev.goldcraft.client.mixin.RenderLayerAccessor;
import dev.goldcraft.client.mixin.RenderParametersAccessor;
import dev.goldcraft.client.mixin.RenderTextureAccessor;
import dev.goldcraft.client.mixin.RenderWriteMaskAccessor;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.particle.ParticleTextureSheet;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumer;
import net.minecraft.client.render.VertexFormat;
import net.minecraft.client.texture.SpriteAtlasTexture;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.Vec3d;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.*;
import java.util.*;

/** Exports the live vanilla particle renderer, including its two custom entity-rendered particle types. */
public final class ParticleExporter {
    private static final List<ParticleTextureSheet> SHEETS=List.of(ParticleTextureSheet.TERRAIN_SHEET,
        ParticleTextureSheet.PARTICLE_SHEET_OPAQUE,ParticleTextureSheet.PARTICLE_SHEET_LIT,
        ParticleTextureSheet.PARTICLE_SHEET_TRANSLUCENT,ParticleTextureSheet.CUSTOM);
    private record Material(Identifier texture,boolean depthWrite) {}
    private final BridgeLink link;
    private final FramePacer pacing=new FramePacer(60);
    private final Map<Identifier,Integer> textures=new LinkedHashMap<>();
    private final Set<Identifier> uploaded=new HashSet<>();
    private final Map<Material,Collector> batches=new LinkedHashMap<>();
    private final Map<String,Integer> kinds=new TreeMap<>();
    private final Map<String,Long> kindFrames=new TreeMap<>();
    private long generation=1,revision,lastFrame,frames,bytes,textureUploads;
    private int count,vertices,culled,unsupported;
    private float tickDelta;
    private double intervalMs;
    private String error="";
    private Vec3d cameraPosition=Vec3d.ZERO;
    private static ParticleExporter active,capture;
    public ParticleExporter(BridgeLink link){this.link=link;active=this;}
    public void reset(){textures.clear();uploaded.clear();batches.clear();kinds.clear();pacing.reset();generation++;lastFrame=0;count=vertices=culled=unsupported=0;}
    public static void atlasUploaded(SpriteAtlasTexture atlas){
        if(active!=null&&atlas.getId().equals(SpriteAtlasTexture.PARTICLE_ATLAS_TEXTURE))active.reset();
    }
    public static boolean capturing(){return capture!=null;}
    public static VertexConsumer captureBuffer(RenderLayer layer){return capture==null?null:capture.buffer(layer);}
    private VertexConsumer buffer(RenderLayer layer){
        if(layer.getDrawMode()!=VertexFormat.DrawMode.QUADS||!(layer instanceof RenderLayer.MultiPhase phase)){
            unsupported++;return new WorldExporter.Collector();
        }
        var parameters=((RenderLayerAccessor)(Object)phase).goldcraft$phases();
        var texture=((RenderTextureAccessor)((RenderParametersAccessor)(Object)parameters).goldcraft$texture()).goldcraft$id();
        if(texture.isEmpty()){unsupported++;return new WorldExporter.Collector();}
        var mask=((RenderParametersAccessor)(Object)parameters).goldcraft$writeMask();
        return batch(new Material(texture.get(),((RenderWriteMaskAccessor)mask).goldcraft$writesDepth()));
    }
    private Collector batch(Material material){
        if(batches.size()>=ParticleSnapshot.MAX_BATCHES&&!batches.containsKey(material))throw new IllegalArgumentException("Particle material budget");
        return batches.computeIfAbsent(material,ignored->new Collector());
    }
    public void frame(long epoch){
        MinecraftClient client=MinecraftClient.getInstance();
        if(!link.connected()||epoch==0||epoch!=GoldCraftClient.HOST.epoch()||!HostInput.hosted()
            ||client.world==null||client.player==null||client.getOverlay()!=null)return;
        long now=System.nanoTime();if(!pacing.ready(now))return;
        if(lastFrame!=0)intervalMs=(now-lastFrame)/1_000_000.0;lastFrame=now;
        long began=Performance.begin();
        try {
            tickDelta=client.getRenderTickCounter().getTickDelta(false);
            var camera=client.gameRenderer.getCamera();
            camera.update(client.world,client.player,!client.options.getPerspective().isFirstPerson(),client.options.getPerspective().isFrontView(),tickDelta);
            cameraPosition=camera.getPos();
            var dispatcher=client.getEntityRenderDispatcher();dispatcher.configure(client.world,camera,client.targetedEntity);
            boolean shadows=((EntityRenderDispatcherAccessor)dispatcher).goldcraft$renderShadows();
            batches.clear();kinds.clear();vertices=count=culled=unsupported=0;
            var particles=((ParticleManagerAccessor)client.particleManager).goldcraft$particles();
            try {
                dispatcher.setRenderShadows(false);
                for(var sheet:SHEETS){
                    var queue=particles.get(sheet);if(queue==null||queue.isEmpty())continue;
                    Identifier atlas=sheet==ParticleTextureSheet.TERRAIN_SHEET?SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE:SpriteAtlasTexture.PARTICLE_ATLAS_TEXTURE;
                    var sink=batch(new Material(atlas,true));
                    for(var particle:queue){
                        if(!particle.isAlive())continue;
                        if(count>=ParticleSnapshot.MAX_PARTICLES||particle.getBoundingBox().getCenter().squaredDistanceTo(cameraPosition)>64*64){culled++;continue;}
                        int before=vertices;
                        // Capture is scoped to this synchronous render call; never intercept vanilla's normal frame.
                        capture=sheet==ParticleTextureSheet.CUSTOM?this:null;
                        try{particle.buildGeometry(sink,camera,tickDelta);}finally{capture=null;}
                        if(vertices>before){
                            count++;
                            String kind=particle.getClass().getSimpleName();
                            if(kinds.size()<128||kinds.containsKey(kind))kinds.merge(kind,1,Integer::sum);
                        }
                    }
                }
                for(var sheet:particles.keySet())if(sheet!=ParticleTextureSheet.NO_RENDER&&!SHEETS.contains(sheet))unsupported+=particles.get(sheet).size();
            }finally{capture=null;dispatcher.setRenderShadows(shadows);}
            List<ParticleSnapshot.Batch> ready=new ArrayList<>();
            for(var entry:batches.entrySet()){
                byte[] mesh=entry.getValue().finish();if(mesh.length==0)continue;
                Identifier id=entry.getKey().texture;int number=0;
                if(!id.equals(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE)){
                    number=textures.computeIfAbsent(id,ignored->textures.size()+1);
                    if(number>ParticleSnapshot.MAX_TEXTURES)throw new IllegalArgumentException("Particle texture budget");
                    if(!uploaded.contains(id)){if(!upload(client,epoch,id,number))return;uploaded.add(id);}
                }
                ready.add(new ParticleSnapshot.Batch(number,entry.getKey().depthWrite?1:0,mesh));
            }
            byte[] payload=ParticleSnapshot.encode(epoch,generation,++revision,HostInput.life(),count,ready);
            if(link.send(Wire.PARTICLE_MESH,payload)){
                frames++;bytes+=payload.length;
                for(String kind:kinds.keySet())if(kindFrames.size()<128||kindFrames.containsKey(kind))kindFrames.merge(kind,1L,Long::sum);
            }
            error="";
        }catch(RuntimeException failure){
            if(!failure.toString().equals(error)){error=failure.toString();GoldCraft.LOGGER.warn("Particle export failed",failure);}
        }finally{Performance.end("particleExport",began);}
    }
    private boolean upload(MinecraftClient client,long epoch,Identifier id,int number){
        int unit=GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);GL13.glActiveTexture(GL13.GL_TEXTURE0);
        int old=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D),pack=GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        int alignment=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT),row=GL11.glGetInteger(GL11.GL_PACK_ROW_LENGTH);
        int rows=GL11.glGetInteger(GL11.GL_PACK_SKIP_ROWS),pixels=GL11.glGetInteger(GL11.GL_PACK_SKIP_PIXELS);
        try{
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,0);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D,client.getTextureManager().getTexture(id).getGlId());
            int w=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_WIDTH),h=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_HEIGHT);
            if(w<1||h<1||w>2048||h>2048)throw new IllegalArgumentException("Particle texture dimensions: "+id);
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1);GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,0);
            GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,0);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,0);
            var buffer=BufferUtils.createByteBuffer(w*h*4);GL11.glGetTexImage(GL11.GL_TEXTURE_2D,0,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,buffer);
            byte[] rgba=new byte[buffer.remaining()];buffer.get(rgba);
            boolean sent=link.send(Wire.PARTICLE_TEXTURE,new Wire.Writer().i64(epoch).i64(generation).i32(number).i32(w).i32(h).bytes(rgba).toByteArray());
            if(sent)textureUploads++;return sent;
        }finally{
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,alignment);GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,row);
            GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,rows);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,pixels);
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,pack);GL11.glBindTexture(GL11.GL_TEXTURE_2D,old);GL13.glActiveTexture(unit);
        }
    }
    public static void diagnostics(JsonObject data){
        if(active==null)return;var p=active;
        data.addProperty("particleFrames",p.frames);data.addProperty("particleBytes",p.bytes);data.addProperty("particleCount",p.count);
        data.addProperty("particleVertices",p.vertices);data.addProperty("particleCulled",p.culled);data.addProperty("particleUnsupported",p.unsupported);
        data.addProperty("particleGeneration",p.generation);data.addProperty("particleTextureUploads",p.textureUploads);
        data.addProperty("particleTickDelta",p.tickDelta);data.addProperty("particleIntervalMs",p.intervalMs);data.addProperty("particleError",p.error);
        JsonObject kinds=new JsonObject(),history=new JsonObject();p.kinds.forEach(kinds::addProperty);p.kindFrames.forEach(history::addProperty);
        data.add("particleKinds",kinds);data.add("particleKindFrames",history);
    }
    private final class Collector implements VertexConsumer {
        private final Wire.Writer out=new Wire.Writer();
        private float x,y,z,u,v;
        private int red=255,green=255,blue=255,alpha=255,lightU=240,lightV=240,count;
        private boolean pending;
        private void flush(){
            if(!pending)return;
            // Match the bridge's host ambient floor while preserving lit flame/lava/end-rod particles.
            float light=.55f+.45f*Math.clamp(Math.max(lightU,lightV)/240f,0,1);
            out.f32(x).f32(y).f32(z).f32(u).f32(v).u8(Math.round(red*light)).u8(Math.round(green*light)).u8(Math.round(blue*light)).u8(alpha);
            pending=false;
        }
        byte[] finish(){flush();if(count%4!=0)throw new IllegalArgumentException("Expected particle quads");return out.toByteArray();}
        public VertexConsumer vertex(float x,float y,float z){
            flush();if(++vertices>ParticleSnapshot.MAX_VERTICES)throw new IllegalArgumentException("Particle vertex budget");
            this.x=x+(float)cameraPosition.x;this.y=y+(float)cameraPosition.y;this.z=z+(float)cameraPosition.z;
            count++;pending=true;return this;
        }
        public VertexConsumer color(int r,int g,int b,int a){red=r;green=g;blue=b;alpha=a;return this;}
        public VertexConsumer texture(float u,float v){this.u=u;this.v=v;return this;}
        public VertexConsumer light(int u,int v){lightU=u;lightV=v;return this;}
        public VertexConsumer overlay(int u,int v){return this;}
        public VertexConsumer normal(float x,float y,float z){return this;}
    }
}
