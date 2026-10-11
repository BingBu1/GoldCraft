package dev.goldcraft.client;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.FramePacer;
import dev.goldcraft.bridge.Performance;
import com.google.gson.JsonObject;
import dev.goldcraft.client.mixin.RenderLayerAccessor;
import dev.goldcraft.client.mixin.RenderParametersAccessor;
import dev.goldcraft.client.mixin.RenderTextureAccessor;
import dev.goldcraft.client.mixin.RenderWriteMaskAccessor;
import dev.goldcraft.client.mixin.EntityRenderDispatcherAccessor;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumer;
import net.minecraft.client.render.VertexConsumerProvider;
import net.minecraft.client.render.VertexFormat;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.client.texture.AbstractTexture;
import net.minecraft.client.texture.SpriteAtlasTexture;
import net.minecraft.util.math.MathHelper;
import net.minecraft.util.Identifier;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import java.util.*;

/** Uses the actual Minecraft entity and block-entity renderers, preserving animated model vertices. */
public final class EntityExporter {
    record Material(Identifier texture,boolean translucent,boolean depthWrite,boolean triangles,boolean viewOffset) {}
    private record TextureStamp(AbstractTexture texture,int glId,long revision) {}
    record Batch(int texture,int flags,WorldExporter.Collector vertices,int bytes) {}
    private final BridgeLink link;
    private final FramePacer pacing=new FramePacer(60);
    private final EntityCollectorBuffers<Material> collectorBuffers=new EntityCollectorBuffers<>();
    private final Map<Identifier,Integer> textures=new LinkedHashMap<>();
    private final Map<Identifier,TextureStamp> uploaded=new HashMap<>();
    private static final Map<AbstractTexture,Long> textureRevisions=new WeakHashMap<>();
    private static AbstractTexture animatedAtlas;
    private static EntityExporter active;
    private long lastFrame,revision,frames,meshBytes,textureUploads,textureBytes,sendFailures;
    private double intervalMs;
    private float tickDelta;
    private int hurtVertices,triangleBatches,wrappedLayers,unsupportedLayers;
    private String lastError="";
    public EntityExporter(BridgeLink link){this.link=link;active=this;}
    static boolean isGlintLayer(RenderLayer layer) {
        return layer==RenderLayer.getArmorEntityGlint()||layer==RenderLayer.getGlintTranslucent()
            ||layer==RenderLayer.getGlint()||layer==RenderLayer.getEntityGlint()
            ||layer==RenderLayer.getDirectEntityGlint();
    }
    public void reset(){collectorBuffers.clear();textures.clear();uploaded.clear();pacing.reset();lastFrame=revision=0;}
    public static void textureChanged(AbstractTexture texture){textureRevisions.merge(texture,1L,Long::sum);}
    public static void beginAtlasAnimation(SpriteAtlasTexture atlas){animatedAtlas=atlas.getId().equals(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE)?null:atlas;}
    public static void endAtlasAnimation(){animatedAtlas=null;}
    public static void spriteUploaded(){if(animatedAtlas!=null)textureChanged(animatedAtlas);}
    public static void diagnostics(JsonObject data){
        if(active==null)return;
        JsonObject e=new JsonObject();e.addProperty("frames",active.frames);e.addProperty("intervalMs",active.intervalMs);
        e.addProperty("tickDelta",active.tickDelta);e.addProperty("meshBytes",active.meshBytes);e.addProperty("textureUploads",active.textureUploads);
        e.addProperty("textureBytes",active.textureBytes);e.addProperty("sendFailures",active.sendFailures);e.addProperty("error",active.lastError);data.add("entityExport",e);
        e.addProperty("hurtVertices",active.hurtVertices);e.addProperty("triangleBatches",active.triangleBatches);
        e.addProperty("wrappedLayers",active.wrappedLayers);e.addProperty("unsupportedLayers",active.unsupportedLayers);
        e.addProperty("collectorBufferRetainedBytes",active.collectorBuffers.retainedBytes());
        e.addProperty("collectorBufferRetainedEntries",active.collectorBuffers.retainedEntries());
        e.addProperty("collectorBufferReuses",active.collectorBuffers.reuses());
        e.addProperty("collectorBufferMisses",active.collectorBuffers.misses());
        e.addProperty("collectorBufferEvictions",active.collectorBuffers.evictions());
        YsmAnimationFrame.diagnostics(e);
    }
    public void frame(long epoch) {
        MinecraftClient client=MinecraftClient.getInstance();
        if(!link.connected()||epoch==0||epoch!=GoldCraftClient.HOST.epoch()||!HostInput.hosted()
            ||client.world==null||client.player==null||client.getOverlay()!=null)return;
        long now=System.nanoTime();if(!pacing.ready(now))return;
        if(lastFrame!=0)intervalMs=(now-lastFrame)/1_000_000.0;lastFrame=now;
        long began=Performance.begin();
        try (var buffers=collectorBuffers.beginFrame()) {
            boolean completed=false;
            try (var effects=DirectMeshCapture.begin(net.minecraft.util.math.Vec3d.ZERO,false)) {
            tickDelta=client.getRenderTickCounter().getTickDelta(false);
            wrappedLayers=unsupportedLayers=0;
            VertexConsumerProvider provider=layer-> {
                buffers.requireOpen();
                // YSM's wrapper disables its own sorting flag while retaining
                // vanilla translucent setup. Read blend/depth/texture from the
                // captured delegate, without running GL setup during export.
                if(layer instanceof RenderLayerDelegate wrapped) {
                    layer=wrapped.goldcraft$delegateLayer();wrappedLayers++;
                }
                // Glint textures are opaque purple. Minecraft masks them with
                // GL_EQUAL against the item's depth and uses a dedicated blend
                // mode and animated texture matrix. Preserve that actual draw.
                if(isGlintLayer(layer))return DirectMeshCapture.entityBuffers().getBuffer(layer);
                boolean triangles=layer.getDrawMode()==VertexFormat.DrawMode.TRIANGLES;
                if((!triangles&&layer.getDrawMode()!=VertexFormat.DrawMode.QUADS)||!(layer instanceof RenderLayer.MultiPhase phase)) {
                    unsupportedLayers++;return new WorldExporter.Collector();
                }
                var parameters=((RenderLayerAccessor)(Object)phase).goldcraft$phases();
                var texture=((RenderTextureAccessor)((RenderParametersAccessor)(Object)parameters).goldcraft$texture()).goldcraft$id();
                // Text/backgrounds are camera-facing world effects, including TLM bubble fills.
                if(texture.isEmpty()||layer.toString().startsWith("RenderType[text"))return DirectMeshCapture.entityBuffers().getBuffer(layer);
                var mask=((RenderParametersAccessor)(Object)parameters).goldcraft$writeMask();
                // Translucent layers can still write depth: vanilla player skins do.
                // Sorting/blending and depth writes are independent material properties.
                Material material=new Material(texture.get(),layer.isTranslucent(),((RenderWriteMaskAccessor)mask).goldcraft$writesDepth(),triangles,
                    LayerSemantics.viewOffset(parameters));
                return buffers.collector(material,triangles);
            };
            var camera=client.gameRenderer.getCamera();
            var dispatcher=client.getEntityRenderDispatcher();dispatcher.configure(client.world,camera,client.targetedEntity);
            int entityCount=0;
            List<HostWorldState.Actor> avatars=new ArrayList<>();
            boolean drawShadows=((EntityRenderDispatcherAccessor)dispatcher).goldcraft$renderShadows();
            dispatcher.setRenderShadows(false);
            boolean ysmFrame=false;
            try (var capture=ModMeshCapture.begin()) {
            ysmFrame=YsmAnimationFrame.beginHosted(GoldCraftClient.presentationOnly(client),client.getRenderTickCounter().getTickDelta(true));
            for(var entity:client.world.getEntities()) {
                if(entity==client.player&&(!HostInput.controlling()||client.options.getPerspective().isFirstPerson())||entity instanceof dev.goldcraft.world.NativePlayerHitboxEntity||entity.isRemoved()||entity.isSpectator()||entity.squaredDistanceTo(client.player)>64*64)continue;
                var actor=GoldCraftClient.HOST.actor(entity.getUuid());
                if(actor!=null&&((actor.flags()&113)!=113||(actor.flags()&12)!=0))continue;
                if(entityCount>=128)break;entityCount++;
                float delta=client.world.getTickManager().shouldSkipTick(entity)?1:tickDelta;
                effects.source("entity:"+entity.getClass().getName());
                // Same interpolation fields used by vanilla WorldRenderer.renderEntity.
                dispatcher.render(entity,MathHelper.lerp(delta,entity.lastRenderX,entity.getX()),
                    MathHelper.lerp(delta,entity.lastRenderY,entity.getY()),MathHelper.lerp(delta,entity.lastRenderZ,entity.getZ()),
                    MathHelper.lerp(delta,entity.prevYaw,entity.getYaw()),delta,new MatrixStack(),provider,dispatcher.getLight(entity,delta));
                if(actor!=null)avatars.add(actor);
            }
            } finally {
                // Renderer uses the real mesh in its shadow maps. Vanilla's
                // ground decal would otherwise remain as a second, fixed blob.
                try {YsmAnimationFrame.endHosted(ysmFrame);}finally {dispatcher.setRenderShadows(drawShadows);}
            }
            HostEpicIndicator.render(client,tickDelta,DirectMeshCapture.entityBuffers());
            var blockDispatcher=client.getBlockEntityRenderDispatcher();blockDispatcher.configure(client.world,camera,client.crosshairTarget);
            int cx=client.player.getBlockX()>>4,cz=client.player.getBlockZ()>>4,blockCount=0;
            for(int x=cx-2;x<=cx+2;x++)for(int z=cz-2;z<=cz+2;z++)if(client.world.isChunkLoaded(x,z)) {
                for(var entity:client.world.getChunk(x,z).getBlockEntities().values()) {
                    if(blockCount>=128||entity.getPos().getSquaredDistance(client.player.getPos())>48*48)continue;
                    MatrixStack matrices=new MatrixStack();var pos=entity.getPos();matrices.translate(pos.getX(),pos.getY(),pos.getZ());
                    effects.source("block_entity:"+entity.getClass().getName());
                    blockDispatcher.render(entity,tickDelta,matrices,provider);blockCount++;
                }
            }
            // Deferred buffers can contain several entities; attribute only immediate draws individually.
            effects.source("entity_buffer_flush");effects.finish();
            List<Batch> ready=new ArrayList<>();int bytes=0;hurtVertices=triangleBatches=0;
            for(var entry:buffers.collectors().entrySet()) {
                var vertices=entry.getValue();int size=vertices.finishSize();if(size==0)continue;
                hurtVertices+=entry.getValue().hurtVertices();if(entry.getKey().triangles)triangleBatches++;
                if(ready.size()>=128||bytes+size>8*1024*1024)throw new IllegalArgumentException("Dynamic mesh budget exceeded");
                Identifier id=entry.getKey().texture;
                // Reuse the block atlas and its CPU animation patches, including
                // animated held/dropped block items, instead of rereading it from GL.
                int number=id.equals(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE)?0:textures.computeIfAbsent(id,ignored->textures.size()+1);
                if(number>256)throw new IllegalArgumentException("Entity texture budget exceeded");
                if(number!=0){
                    var texture=client.getTextureManager().getTexture(id);
                    var stamp=new TextureStamp(texture,texture.getGlId(),textureRevisions.getOrDefault(texture,0L));
                    if(!stamp.equals(uploaded.get(id))){if(!upload(epoch,stamp,id,number)){sendFailures++;return;}uploaded.put(id,stamp);}
                }
                int flags=(entry.getKey().translucent?1:0)|(entry.getKey().depthWrite?2:0)|(entry.getKey().viewOffset?4:0);
                ready.add(new Batch(number,flags,vertices,size));bytes+=size;
            }
            byte[] payload=encodeBase(epoch,++revision,entityCount,blockCount,ready,avatars);
            if(ParticleExporter.sendEntityFrame(epoch,HostInput.life(),revision,payload,effects)){frames++;meshBytes+=payload.length;lastError="";completed=true;}else sendFailures++;
            }
            // Effects close/restore first. Any early return or failed close
            // discards this frame's buffers instead of retaining partial work.
            if(completed)buffers.complete();
        }catch(RuntimeException e){if(!e.toString().equals(lastError)){lastError=e.toString();GoldCraft.LOGGER.warn("Entity export failed",e);}}
        finally{Performance.end("entityExport",began);}
    }
    static byte[] encodeBase(long epoch,long revision,int entityCount,int blockCount,List<Batch> ready,List<HostWorldState.Actor> avatars) {
        int bytes=0;
        if(ready.size()>128)throw new IllegalArgumentException("Dynamic mesh budget exceeded");
        for(Batch batch:ready){
            if(batch.bytes<0||batch.bytes>8*1024*1024-bytes)throw new IllegalArgumentException("Dynamic mesh budget exceeded");
            bytes+=batch.bytes;
        }
        int capacity=Math.toIntExact(32L+12L*ready.size()+bytes+28L*avatars.size());
        Wire.Writer writer=new Wire.Writer(capacity).i64(epoch).i64(revision).i32(entityCount).i32(blockCount).i32(ready.size());
        for(Batch batch:ready){
            writer.i32(batch.texture).i32(batch.flags).i32(batch.bytes/28);
            batch.vertices.writeTo(writer,batch.bytes);
        }
        // Publish identities atomically with the mesh. A prior mesh must never
        // suppress a new CS player who happens to occupy the same slot.
        writer.i32(avatars.size());
        for(var actor:avatars)writer.i32(actor.slot()).i32(actor.serial()).i32(actor.userid()).bytes(Wire.uuid(actor.minecraftPlayer()));
        return writer.toByteArray();
    }
    private boolean upload(long epoch,TextureStamp stamp,Identifier id,int number) {
        int unit=GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);GL13.glActiveTexture(GL13.GL_TEXTURE0);
        int old=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D),alignment=GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT);
        try {
            GL11.glBindTexture(GL11.GL_TEXTURE_2D,stamp.glId());
            int width=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_WIDTH),height=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_HEIGHT);
            if(width<1||height<1||width>2048||height>2048)throw new IllegalArgumentException("Entity texture dimensions: "+id);
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1);var pixels=BufferUtils.createByteBuffer(width*height*4);
            GL11.glGetTexImage(GL11.GL_TEXTURE_2D,0,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,pixels);byte[] bytes=new byte[pixels.remaining()];pixels.get(bytes);
            boolean sent=link.send(Wire.ENTITY_TEXTURE,new Wire.Writer().i64(epoch).i32(number).i32(width).i32(height).bytes(bytes).toByteArray());
            if(sent){textureUploads++;textureBytes+=bytes.length;}return sent;
        }finally {GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,alignment);GL11.glBindTexture(GL11.GL_TEXTURE_2D,old);GL13.glActiveTexture(unit);}
    }
}
