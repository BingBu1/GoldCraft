package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.ParticleSnapshot;
import dev.goldcraft.bridge.ParticleTextureChunks;
import dev.goldcraft.bridge.EntityFrame;
import dev.goldcraft.bridge.PostEffectPlan;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.bridge.FramePacer;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.client.mixin.EntityRenderDispatcherAccessor;
import dev.goldcraft.client.mixin.ParticleManagerAccessor;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleTextureSheet;
import net.minecraft.client.texture.SpriteAtlasTexture;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.*;
import org.lwjgl.system.MemoryUtil;
import java.util.*;

/** Ordered world effects from real entity draws and every supported registered particle sheet. */
public final class ParticleExporter {
    private final BridgeLink link;
    private final FramePacer pacing=new FramePacer(60);
    private final Map<Integer,Integer> textures=new LinkedHashMap<>();
    private final Map<Integer,Long> uploaded=new HashMap<>();
    private final Map<String,Integer> kinds=new TreeMap<>(),unsupportedReasons=new TreeMap<>();
    private final Map<String,Integer> shaders=new TreeMap<>();
    private final Map<String,Integer> sources=new TreeMap<>(),particleMods=new TreeMap<>();
    private final Map<String,Long> kindFrames=new TreeMap<>();
    private Set<Integer> entityTextureSet=Set.of(),worldTextureSet=Set.of();
    private long entityEffectFrames,entityEffectBytes;
    private int entityEffectDraws;
    private long generation=1,revision,lastFrame,frames,bytes,textureUploads,postFrames,postBytes;
    private int postStages,postVertices;
    private int count,vertices,culled,unsupported;
    private float tickDelta;
    private double intervalMs;
    private String error="";
    private ParticleTextureUpload pendingTexture;
    private long textureLinkGeneration=-1,textureTransferRevision,textureEpoch;
    private int textureLife;
    private boolean textureContextKnown;
    private final Set<Integer> diagnosedLargeTextures=new HashSet<>();
    private static ParticleExporter active,rendering;
    public ParticleExporter(BridgeLink link){this.link=link;active=this;}
    public void reset(){
        cancelTextureUpload();
        diagnosedLargeTextures.clear();
        textureContextKnown=false;
        textures.clear();uploaded.clear();kinds.clear();shaders.clear();sources.clear();particleMods.clear();unsupportedReasons.clear();pacing.reset();EffectTextures.clear();
        entityTextureSet=worldTextureSet=Set.of();entityEffectDraws=0;generation++;lastFrame=0;count=vertices=culled=unsupported=postStages=postVertices=0;
        VixPostCapture.resourcesChanged();
        if(link.connected()&&GoldCraftClient.HOST.epoch()!=0&&HostInput.life()!=0)clear(GoldCraftClient.HOST.epoch());
    }
    public static void atlasUploaded(SpriteAtlasTexture atlas){if(active!=null)active.reset();}
    public static boolean sendEntityFrame(long epoch,int life,long entityRevision,byte[] payload,DirectMeshCapture effects){
        if(active==null||!active.link.connected()||epoch==0||life==0||epoch!=GoldCraftClient.HOST.epoch()||life!=HostInput.life())return false;
        var exporter=active;exporter.prepareTextureContext(epoch,life);
        var draws=effects.draws();
        int atlas=MinecraftClient.getInstance().getTextureManager().getTexture(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE).getGlId();
        var used=textureUse(draws,atlas);exporter.entityTextureSet=used;
        if(!exporter.prepareTextures(epoch,used,0))return false;
        var ready=exporter.effectBatches(draws,atlas);
        byte[] frame=EntityFrame.encode(epoch,exporter.generation,entityRevision,life,payload,ready);
        if(!exporter.link.send(Wire.ENTITY_FRAME,frame))return false;
        exporter.entityEffectFrames++;exporter.entityEffectBytes+=frame.length;exporter.entityEffectDraws=draws.size();
        return true;
    }
    private void prepareTextureContext(long epoch,int life){
        if(textureLinkGeneration!=link.generation()||(textureContextKnown&&textureEpoch!=epoch)){
            reset();textureLinkGeneration=link.generation();
        }
        if(textureContextKnown&&textureLife!=life){cancelTextureUpload();uploaded.clear();}
        textureEpoch=epoch;textureLife=life;textureContextKnown=true;
        if(pendingTexture!=null&&!pendingTexture.matches(epoch,generation,life))cancelTextureUpload();
    }
    private static Set<Integer> textureUse(List<DirectMeshCapture.Draw> draws,int atlas){
        var used=new HashSet<Integer>();
        for(var draw:draws)if(draw.glTexture()!=ParticleSnapshot.WHITE_TEXTURE&&draw.glTexture()!=atlas)used.add(draw.glTexture());
        return used;
    }
    private boolean prepareTextures(long epoch,Set<Integer> used,int completedTexture){
        var union=new HashSet<>(entityTextureSet);union.addAll(worldTextureSet);
        if(union.size()>ParticleSnapshot.MAX_TEXTURES)throw new IllegalArgumentException("Combined entity/world effect texture budget");
        if(textures.size()+union.stream().filter(id->!textures.containsKey(id)).count()>ParticleSnapshot.MAX_TEXTURES){
            cancelTextureUpload();textures.clear();uploaded.clear();diagnosedLargeTextures.clear();EffectTextures.clear();generation++;completedTexture=0;
        }
        boolean ready=true;
        for(int id:used){
            int number=textures.computeIfAbsent(id,ignored->textures.size()+1);
            long stamp=EffectTextures.revision(id);
            if(id!=completedTexture&&pendingTexture==null&&!Objects.equals(uploaded.get(id),stamp)){
                if(upload(epoch,id,number,stamp))uploaded.put(id,stamp);
            }
            if(!uploaded.containsKey(id))ready=false;
        }
        return ready;
    }
    private List<ParticleSnapshot.Batch> effectBatches(List<DirectMeshCapture.Draw> draws,int atlas){
        List<ParticleSnapshot.Batch> result=new ArrayList<>();int total=0;
        for(var draw:draws){
            total+=draw.vertices().length/24;
            if(total>ParticleSnapshot.MAX_VERTICES||result.size()>=ParticleSnapshot.MAX_BATCHES)
                throw new IllegalArgumentException("Entity effect mesh budget");
            int id=draw.glTexture(),number=id==ParticleSnapshot.WHITE_TEXTURE?ParticleSnapshot.WHITE_TEXTURE:id==atlas?0:textures.get(id);
            result.add(new ParticleSnapshot.Batch(number,draw.material(),draw.vertices()));
        }
        return result;
    }
    /** Called only at the real ParticleManager render invocation, leaving standalone particles untouched. */
    public static boolean allow(Particle particle){
        if(rendering==null)return true;
        var exporter=rendering;
        var box=particle.getBoundingBox();
        var center=box.getCenter();
        // Trail render bounds can be infinite. Their physical bounds retain the emitter position.
        if(!particle.isAlive()||exporter.count>=ParticleSnapshot.MAX_PARTICLES
            ||!Double.isFinite(center.x)||!Double.isFinite(center.y)||!Double.isFinite(center.z)
            ||center.squaredDistanceTo(MinecraftClient.getInstance().gameRenderer.getCamera().getPos())>64*64){exporter.culled++;return false;}
        exporter.count++;
        String kind=particle.getClass().getSimpleName();
        if(exporter.kinds.size()<128||exporter.kinds.containsKey(kind))exporter.kinds.merge(kind,1,Integer::sum);
        String namespace=particle.getClass().getPackageName();
        if(exporter.particleMods.size()<128||exporter.particleMods.containsKey(namespace))exporter.particleMods.merge(namespace,1,Integer::sum);
        return true;
    }
    public void frame(long epoch){
        var client=MinecraftClient.getInstance();
        if(!link.connected()||epoch==0||epoch!=GoldCraftClient.HOST.epoch()||!HostInput.hosted()
            ||client.world==null||client.player==null||client.getOverlay()!=null){cancelTextureUpload();return;}
        prepareTextureContext(epoch,HostInput.life());
        long now=System.nanoTime();if(!pacing.ready(now))return;
        if(lastFrame!=0)intervalMs=(now-lastFrame)/1_000_000.0;lastFrame=now;
        long began=Performance.begin();
        try {
            int completedTexture=0;
            if(pendingTexture!=null&&pendingTexture.pump(data->link.send(Wire.PARTICLE_TEXTURE_CHUNK,data))) {
                completedTexture=pendingTexture.texture();
                uploaded.put(completedTexture,pendingTexture.stamp());textureUploads++;
                cancelTextureUpload();
            }
            tickDelta=client.getRenderTickCounter().getTickDelta(false);
            var camera=client.gameRenderer.getCamera();
            var dispatcher=client.getEntityRenderDispatcher();dispatcher.configure(client.world,camera,client.targetedEntity);
            boolean shadows=((EntityRenderDispatcherAccessor)dispatcher).goldcraft$renderShadows();
            kinds.clear();shaders.clear();sources.clear();particleMods.clear();unsupportedReasons.clear();vertices=count=culled=unsupported=0;
            List<DirectMeshCapture.Draw> draws=new ArrayList<>();
            boolean stage=false;
            try (var cpu=ModMeshCapture.begin();var capture=DirectMeshCapture.begin(camera.getPos(),true)) {
                // Snapshot producers may invoke mesh APIs as well as enqueue particles.
                // Their full lifecycle must see the same CPU fallback and capture scope.
                capture.source("hosted_after_entities");stage=HostedEffectStages.beforeParticles(client);
                capture.source("particles");
                dispatcher.setRenderShadows(false);rendering=this;
                client.particleManager.render(client.gameRenderer.getLightmapTextureManager(),camera,tickDelta,null,sheet->{
                    return !HostedEffectStages.isUnsupportedPostEffect(sheet);
                });
                capture.finish();draws.addAll(capture.draws());
                capture.unsupported().forEach((key,value)->unsupportedReasons.merge(key,value,Integer::sum));
                capture.shaders().forEach((key,value)->shaders.merge(key,value,Integer::sum));
                capture.sources().forEach((key,value)->sources.merge(key,value,Integer::sum));
            }finally{rendering=null;dispatcher.setRenderShadows(shadows);HostedEffectStages.afterParticles(stage);}
            // This second invocation is post-only. Each real particle renderer still runs once;
            // VIX's own tail owns its shader/layout/sorting and never joins ordinary draws.
            VixPostCapture.Frame post=VixPostCapture.capture(client,tickDelta);
            postStages=post.stages().size();postVertices=0;
            unsupported=unsupportedReasons.values().stream().mapToInt(Integer::intValue).sum();
            int blockAtlas=client.getTextureManager().getTexture(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE).getGlId();
            var used=textureUse(draws,blockAtlas);
            for(var draw:post.draws())if(draw.glTexture()!=ParticleSnapshot.WHITE_TEXTURE&&draw.glTexture()!=blockAtlas)used.add(draw.glTexture());
            worldTextureSet=used;
            if(!prepareTextures(epoch,used,completedTexture)){clear(epoch);error="";return;}
            List<ParticleSnapshot.Batch> ready=new ArrayList<>(),postReady=new ArrayList<>();
            // A snapshot is atomic: never retain a depth-only half of an over-budget afterimage.
            for(var draw:draws){
                vertices+=draw.vertices().length/24;
                if(vertices>ParticleSnapshot.MAX_VERTICES||ready.size()>=ParticleSnapshot.MAX_BATCHES)
                    throw new IllegalArgumentException("Combined world effect budget");
                int id=draw.glTexture(),number;
                if(id==ParticleSnapshot.WHITE_TEXTURE)number=ParticleSnapshot.WHITE_TEXTURE;
                else if(id==blockAtlas)number=0;
                else {
                    number=textures.get(id);
                    if(number>ParticleSnapshot.MAX_TEXTURES)throw new IllegalArgumentException("Effect texture budget");
                }
                ready.add(new ParticleSnapshot.Batch(number,draw.material(),draw.vertices()));
            }
            for(var draw:post.draws()) {
                postVertices+=draw.vertices().length/24;
                if(postVertices>ParticleSnapshot.MAX_VERTICES||postReady.size()>=ParticleSnapshot.MAX_BATCHES)
                    throw new IllegalArgumentException("Post effect mesh budget");
                int id=draw.glTexture(),number=id==ParticleSnapshot.WHITE_TEXTURE?ParticleSnapshot.WHITE_TEXTURE:id==blockAtlas?0:textures.get(id);
                postReady.add(new ParticleSnapshot.Batch(number,draw.material(),draw.vertices()));
            }
            int snapshotCount=ready.isEmpty()?0:Math.max(1,count);
            byte[] payload=ParticleSnapshot.encode(epoch,generation,++revision,HostInput.life(),snapshotCount,ready);
            byte[] postPayload=PostEffectPlan.encode(epoch,generation,revision,HostInput.life(),post.count(),post.stages(),post.depthResets(),postReady);
            if(link.send(Wire.PARTICLE_MESH,payload)){
                frames++;bytes+=payload.length;
                for(String kind:kinds.keySet())if(kindFrames.size()<128||kindFrames.containsKey(kind))kindFrames.merge(kind,1L,Long::sum);
            }else throw new IllegalStateException("World effect mesh not queued");
            if(link.send(Wire.POST_EFFECT_PLAN,postPayload)){postFrames++;postBytes+=postPayload.length;}
            else throw new IllegalStateException("Post effect plan not queued");
            error="";
        }catch(RuntimeException failure){
            if(!failure.toString().equals(error)){error=failure.toString();GoldCraft.LOGGER.warn("World effect export failed",failure);}
            // Failed snapshots are removals, never permission to retain a prior mask/half draw.
            if(HostInput.life()!=0)clear(epoch);
        }finally{Performance.end("particleExport",began);}
    }
    private void clear(long epoch) {
        long clearRevision=++revision;
        link.send(Wire.PARTICLE_MESH,ParticleSnapshot.encode(epoch,generation,clearRevision,HostInput.life(),0,List.of()));
        link.send(Wire.POST_EFFECT_PLAN,PostEffectPlan.encode(epoch,generation,clearRevision,HostInput.life(),0,List.of(),List.of(),List.of()));
    }
    private void cancelTextureUpload(){if(pendingTexture!=null){pendingTexture.close();pendingTexture=null;}}
    private boolean upload(long epoch,int glTexture,int number,long stamp){
        int unit=GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);GL13.glActiveTexture(GL13.GL_TEXTURE0);
        int old=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D),pack=GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        int alignment=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT),row=GL11.glGetInteger(GL11.GL_PACK_ROW_LENGTH);
        int rows=GL11.glGetInteger(GL11.GL_PACK_SKIP_ROWS),pixels=GL11.glGetInteger(GL11.GL_PACK_SKIP_PIXELS);
        try{
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,0);
            boolean existing=GL11.glIsTexture(glTexture);
            if(!existing)throw new IllegalArgumentException(textureDiagnostic(glTexture,0,0,false));
            GL11.glBindTexture(GL11.GL_TEXTURE_2D,glTexture);
            int w=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_WIDTH),h=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_HEIGHT);
            if(w<1||h<1||w>ParticleTextureChunks.MAX_DIMENSION||h>ParticleTextureChunks.MAX_DIMENSION)
                throw new IllegalArgumentException(textureDiagnostic(glTexture,w,h,existing));
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1);GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,0);
            GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,0);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,0);
            if(w>2048||h>2048) {
                var buffer=MemoryUtil.memAlloc(w*h*4);
                try {
                    GL11.glGetTexImage(GL11.GL_TEXTURE_2D,0,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,buffer);
                    int readError=GL11.glGetError();
                    if(readError!=GL11.GL_NO_ERROR)throw new IllegalStateException("Effect texture readback GL="+readError+" "+textureDiagnostic(glTexture,w,h,existing));
                    pendingTexture=new ParticleTextureUpload(new ParticleTextureChunks.Header(epoch,generation,HostInput.life(),number,++textureTransferRevision,w,h),
                        glTexture,stamp,buffer,()->MemoryUtil.memFree(buffer));
                }catch(RuntimeException|Error failure){MemoryUtil.memFree(buffer);throw failure;}
                if(diagnosedLargeTextures.add(glTexture))GoldCraft.LOGGER.info("Staging large world effect texture: {}",textureDiagnostic(glTexture,w,h,existing));
                return false;
            }
            var buffer=BufferUtils.createByteBuffer(w*h*4);GL11.glGetTexImage(GL11.GL_TEXTURE_2D,0,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,buffer);
            int readError=GL11.glGetError();
            if(readError!=GL11.GL_NO_ERROR)throw new IllegalStateException("Effect texture readback GL="+readError+" "+textureDiagnostic(glTexture,w,h,existing));
            byte[] rgba=new byte[buffer.remaining()];buffer.get(rgba);
            boolean sent=link.send(Wire.PARTICLE_TEXTURE,new Wire.Writer().i64(epoch).i64(generation).i32(number).i32(w).i32(h).bytes(rgba).toByteArray());
            if(!sent)throw new IllegalStateException("Effect texture upload not queued");
            textureUploads++;return true;
        }finally{
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,alignment);GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,row);
            GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,rows);GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,pixels);
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,pack);GL11.glBindTexture(GL11.GL_TEXTURE_2D,old);GL13.glActiveTexture(unit);
        }
    }
    private static String textureDiagnostic(int id,int width,int height,boolean existing){
        var textures=MinecraftClient.getInstance().getTextureManager();
        var atlas=textures.getOrDefault(SpriteAtlasTexture.PARTICLE_ATLAS_TEXTURE,null);
        int atlasId=atlas==null?0:atlas.getGlId();
        String resource=id==atlasId?SpriteAtlasTexture.PARTICLE_ATLAS_TEXTURE.toString():"unresolved";
        return "Particle texture dimensions: id="+id+" width="+width+" height="+height+" existing="+existing
            +" particleAtlasId="+atlasId+" resource="+resource;
    }
    public static void diagnostics(JsonObject data){
        if(active==null)return;var p=active;
        data.addProperty("particleFrames",p.frames);data.addProperty("particleBytes",p.bytes);data.addProperty("particleCount",p.count);
        data.addProperty("particleVertices",p.vertices);data.addProperty("particleCulled",p.culled);data.addProperty("particleUnsupported",p.unsupported);
        data.addProperty("particleGeneration",p.generation);data.addProperty("particleTextureUploads",p.textureUploads);
        data.addProperty("particleTexturePending",p.pendingTexture!=null);
        data.addProperty("particleTexturePendingId",p.pendingTexture==null?0:p.pendingTexture.texture());
        data.addProperty("particleTexturePendingBytes",p.pendingTexture==null?0:p.pendingTexture.sentBytes());
        data.addProperty("particleTexturePendingTotal",p.pendingTexture==null?0:p.pendingTexture.header().totalBytes());
        data.addProperty("particleTickDelta",p.tickDelta);data.addProperty("particleIntervalMs",p.intervalMs);data.addProperty("particleError",p.error);
        data.addProperty("postEffectFrames",p.postFrames);data.addProperty("postEffectBytes",p.postBytes);
        data.addProperty("postEffectStages",p.postStages);data.addProperty("postEffectVertices",p.postVertices);
        JsonObject kinds=new JsonObject(),history=new JsonObject(),reasons=new JsonObject(),programs=new JsonObject(),origins=new JsonObject(),mods=new JsonObject();
        p.kinds.forEach(kinds::addProperty);p.kindFrames.forEach(history::addProperty);p.unsupportedReasons.forEach(reasons::addProperty);
        data.add("particleKinds",kinds);data.add("particleKindFrames",history);data.add("effectUnsupportedReasons",reasons);
        p.shaders.forEach(programs::addProperty);data.add("effectShaderDraws",programs);
        p.sources.forEach(origins::addProperty);data.add("effectDrawSources",origins);
        p.particleMods.forEach(mods::addProperty);data.add("particlePackages",mods);
        data.addProperty("effectTextureInvalidations",EffectTextures.invalidations());
        data.addProperty("entityEffectDraws",p.entityEffectDraws);data.addProperty("entityEffectFrames",p.entityEffectFrames);data.addProperty("entityEffectBytes",p.entityEffectBytes);
        HostedEffectStages.diagnostics(data);ModMeshCapture.diagnostics(data);VixPostCapture.diagnostics(data);
    }
}
