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
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleTextureSheet;
import net.minecraft.client.texture.SpriteAtlasTexture;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.*;
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
    private List<DirectMeshCapture.Draw> entityEffects=List.of();
    private Map<String,Integer> entityUnsupported=Map.of();
    private Map<String,Integer> entityShaders=Map.of();
    private Map<String,Integer> entitySources=Map.of();
    private long entityEpoch,entityTime;
    private int entityLife;
    private long generation=1,revision,lastFrame,frames,bytes,textureUploads;
    private int count,vertices,culled,unsupported;
    private float tickDelta;
    private double intervalMs;
    private String error="";
    private static ParticleExporter active,rendering;
    public ParticleExporter(BridgeLink link){this.link=link;active=this;}
    public void reset(){
        textures.clear();uploaded.clear();kinds.clear();shaders.clear();sources.clear();particleMods.clear();unsupportedReasons.clear();pacing.reset();EffectTextures.clear();
        entityEffects=List.of();entityUnsupported=entityShaders=entitySources=Map.of();entityEpoch=entityTime=0;generation++;lastFrame=0;count=vertices=culled=unsupported=0;
    }
    public static void atlasUploaded(SpriteAtlasTexture atlas){if(active!=null)active.reset();}
    static void entityEffects(long epoch,int life,DirectMeshCapture capture){
        if(active==null)return;
        active.entityEffects=capture.draws();active.entityUnsupported=capture.unsupported();
        active.entityShaders=capture.shaders();
        active.entitySources=capture.sources();
        active.entityEpoch=epoch;active.entityLife=life;active.entityTime=System.nanoTime();
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
            ||client.world==null||client.player==null||client.getOverlay()!=null)return;
        long now=System.nanoTime();if(!pacing.ready(now))return;
        if(lastFrame!=0)intervalMs=(now-lastFrame)/1_000_000.0;lastFrame=now;
        long began=Performance.begin();
        try {
            tickDelta=client.getRenderTickCounter().getTickDelta(false);
            var camera=client.gameRenderer.getCamera();
            var dispatcher=client.getEntityRenderDispatcher();dispatcher.configure(client.world,camera,client.targetedEntity);
            boolean shadows=((EntityRenderDispatcherAccessor)dispatcher).goldcraft$renderShadows();
            kinds.clear();shaders.clear();sources.clear();particleMods.clear();unsupportedReasons.clear();vertices=count=culled=unsupported=0;
            List<DirectMeshCapture.Draw> draws=new ArrayList<>();
            if(entityEpoch==epoch&&entityLife==HostInput.life()&&now-entityTime<250_000_000L){
                draws.addAll(entityEffects);unsupportedReasons.putAll(entityUnsupported);
                shaders.putAll(entityShaders);
                sources.putAll(entitySources);
            }
            boolean stage=false;
            try (var cpu=ModMeshCapture.begin();var capture=DirectMeshCapture.begin(camera.getPos(),true)) {
                // Snapshot producers may invoke mesh APIs as well as enqueue particles.
                // Their full lifecycle must see the same CPU fallback and capture scope.
                capture.source("hosted_after_entities");stage=HostedEffectStages.beforeParticles(client);
                capture.source("particles");
                dispatcher.setRenderShadows(false);rendering=this;
                var queues=((ParticleManagerAccessor)client.particleManager).goldcraft$particles();
                client.particleManager.render(client.gameRenderer.getLightmapTextureManager(),camera,tickDelta,null,sheet->{
                    if(HostedEffectStages.isUnsupportedPostEffect(sheet)){
                        unsupportedReasons.merge("post_processing:"+sheet.getClass().getSimpleName(),queues.get(sheet).size(),Integer::sum);return false;
                    }
                    return true;
                });
                capture.finish();draws.addAll(capture.draws());
                capture.unsupported().forEach((key,value)->unsupportedReasons.merge(key,value,Integer::sum));
                capture.shaders().forEach((key,value)->shaders.merge(key,value,Integer::sum));
                capture.sources().forEach((key,value)->sources.merge(key,value,Integer::sum));
            }finally{rendering=null;dispatcher.setRenderShadows(shadows);HostedEffectStages.afterParticles(stage);}
            unsupported=unsupportedReasons.values().stream().mapToInt(Integer::intValue).sum();
            int blockAtlas=client.getTextureManager().getTexture(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE).getGlId();
            var used=new HashSet<Integer>();
            for(var draw:draws)if(draw.glTexture()!=ParticleSnapshot.WHITE_TEXTURE&&draw.glTexture()!=blockAtlas)used.add(draw.glTexture());
            if(used.size()>ParticleSnapshot.MAX_TEXTURES)throw new IllegalArgumentException("Current effect texture budget");
            if(textures.size()+used.stream().filter(id->!textures.containsKey(id)).count()>ParticleSnapshot.MAX_TEXTURES){
                // Retire no-longer-used slots atomically with a new generation, including GL-name reuse.
                textures.clear();uploaded.clear();EffectTextures.clear();generation++;
            }
            List<ParticleSnapshot.Batch> ready=new ArrayList<>();
            // A snapshot is atomic: never retain a depth-only half of an over-budget afterimage.
            for(var draw:draws){
                vertices+=draw.vertices().length/24;
                if(vertices>ParticleSnapshot.MAX_VERTICES||ready.size()>=ParticleSnapshot.MAX_BATCHES)
                    throw new IllegalArgumentException("Combined world effect budget");
                int id=draw.glTexture(),number;
                if(id==ParticleSnapshot.WHITE_TEXTURE)number=ParticleSnapshot.WHITE_TEXTURE;
                else if(id==blockAtlas)number=0;
                else {
                    number=textures.computeIfAbsent(id,ignored->textures.size()+1);
                    if(number>ParticleSnapshot.MAX_TEXTURES)throw new IllegalArgumentException("Effect texture budget");
                    long stamp=EffectTextures.revision(id);
                    if(!Objects.equals(uploaded.get(id),stamp)){if(!upload(epoch,id,number))return;uploaded.put(id,stamp);}
                }
                ready.add(new ParticleSnapshot.Batch(number,draw.material(),draw.vertices()));
            }
            int snapshotCount=ready.isEmpty()?0:Math.max(1,count);
            byte[] payload=ParticleSnapshot.encode(epoch,generation,++revision,HostInput.life(),snapshotCount,ready);
            if(link.send(Wire.PARTICLE_MESH,payload)){
                frames++;bytes+=payload.length;
                for(String kind:kinds.keySet())if(kindFrames.size()<128||kindFrames.containsKey(kind))kindFrames.merge(kind,1L,Long::sum);
            }
            error="";
        }catch(RuntimeException failure){
            if(!failure.toString().equals(error)){error=failure.toString();GoldCraft.LOGGER.warn("World effect export failed",failure);}
        }finally{Performance.end("particleExport",began);}
    }
    private boolean upload(long epoch,int glTexture,int number){
        int unit=GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);GL13.glActiveTexture(GL13.GL_TEXTURE0);
        int old=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D),pack=GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        int alignment=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT),row=GL11.glGetInteger(GL11.GL_PACK_ROW_LENGTH);
        int rows=GL11.glGetInteger(GL11.GL_PACK_SKIP_ROWS),pixels=GL11.glGetInteger(GL11.GL_PACK_SKIP_PIXELS);
        try{
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,0);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D,glTexture);
            int w=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_WIDTH),h=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_HEIGHT);
            if(w<1||h<1||w>2048||h>2048)throw new IllegalArgumentException("Particle texture dimensions: "+glTexture);
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
        JsonObject kinds=new JsonObject(),history=new JsonObject(),reasons=new JsonObject(),programs=new JsonObject(),origins=new JsonObject(),mods=new JsonObject();
        p.kinds.forEach(kinds::addProperty);p.kindFrames.forEach(history::addProperty);p.unsupportedReasons.forEach(reasons::addProperty);
        data.add("particleKinds",kinds);data.add("particleKindFrames",history);data.add("effectUnsupportedReasons",reasons);
        p.shaders.forEach(programs::addProperty);data.add("effectShaderDraws",programs);
        p.sources.forEach(origins::addProperty);data.add("effectDrawSources",origins);
        p.particleMods.forEach(mods::addProperty);data.add("particlePackages",mods);
        data.addProperty("effectTextureInvalidations",EffectTextures.invalidations());
        data.addProperty("entityEffectDraws",p.entityEffects.size());HostedEffectStages.diagnostics(data);ModMeshCapture.diagnostics(data);
    }
}
