package dev.goldcraft.client;

import com.mojang.blaze3d.systems.RenderSystem;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.bridge.AtlasAnimationState;
import net.minecraft.block.BlockRenderType;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.RenderLayers;
import net.minecraft.client.render.VertexConsumer;
import net.minecraft.client.texture.SpriteAtlasTexture;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.ChunkSectionPos;
import net.minecraft.util.math.random.Random;
import org.lwjgl.BufferUtils;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL13;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.*;

/** Exports actual baked Minecraft block/fluid quads in atlas UV coordinates. */
public final class WorldExporter {
    private record Section(int x,int y,int z) {}
    private record Lamp(float x,float y,float z,float radius,int color) {}
    private final BridgeLink link;
    private final EntityExporter entities;
    private final ParticleExporter particles;
    private final BlockFeedbackExporter feedback;
    private final LinkedHashSet<Section> dirty=new LinkedHashSet<>();
    private final LinkedHashSet<Section> changed=new LinkedHashSet<>();
    private final Map<Section,byte[][]> sent=new HashMap<>();
    private final Map<Section,List<Lamp>> lamps=new HashMap<>();
    private ClientWorld sourceWorld;
    private long worldEpoch, revision, lastRefresh, lastLights,lastRescan,atlasGeneration;
    private String lastError="";
    private boolean atlasSent;
    private boolean captureAnimation;
    private SpriteAtlasTexture sourceAtlas;
    private final AtlasAnimationState animations=new AtlasAnimationState();
    private static WorldExporter active;

    public WorldExporter(BridgeLink link) { this.link=link;entities=new EntityExporter(link);particles=new ParticleExporter(link);feedback=new BlockFeedbackExporter(link); active=this; }
    public void presentationFrame(){
        if(atlasSent&&sourceWorld==MinecraftClient.getInstance().world){entities.frame(worldEpoch);particles.frame(worldEpoch);feedback.frame(worldEpoch);}
    }
    public void bind(long epoch) {
        if(epoch!=worldEpoch) { reset(); worldEpoch=epoch; }
    }
    public void resendScene(long epoch) {
        if(epoch==worldEpoch&&epoch!=0){sourceWorld=null;atlasSent=false;captureAnimation=false;}
    }
    public void reset() {
        dirty.clear(); changed.clear(); sent.clear();lamps.clear();entities.reset();particles.reset(); sourceWorld=null; worldEpoch=0; revision=lastRefresh=lastLights=lastRescan=0; atlasSent=false;
        captureAnimation=false;sourceAtlas=null;animations.reset(0,0);
    }
    public static void atlasUploaded(SpriteAtlasTexture atlas) {
        if(active!=null&&atlas.getId().equals(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE)) {
            // Resource reload can move sprite UVs without replacing the atlas object.
            active.sourceWorld=null;active.atlasSent=false;active.captureAnimation=false;
        }
    }
    public static void beginAtlasAnimation(SpriteAtlasTexture atlas) {
        if(active!=null)active.captureAnimation=active.atlasSent&&active.sourceAtlas==atlas&&active.link.connected();
    }
    public static void endAtlasAnimation() {if(active!=null)active.captureAnimation=false;}
    public static void spriteUploaded(int x,int y,int sourceX,int sourceY,int width,int height,NativeImage image) {
        WorldExporter exporter=active;if(exporter==null||!exporter.captureAnimation)return;
        long began=Performance.begin();
        try {
            long size=(long)width*height*4;
            if(size<=0||size>AtlasAnimationState.MAX_BYTES)throw new IllegalArgumentException("Animated sprite exceeds transport budget");
            ByteBuffer pixels=ByteBuffer.allocate((int)size).order(ByteOrder.LITTLE_ENDIAN);
            // NativeImage's ABGR integer is RGBA in little-endian memory, including alpha.
            // Read Minecraft's real frame/interpolation output, without a GPU readback.
            for(int row=0;row<height;row++)for(int col=0;col<width;col++)pixels.putInt(image.getColor(sourceX+col,sourceY+row));
            exporter.animations.update(x,y,width,height,pixels.array());
        }catch(RuntimeException e) {
            if(!e.toString().equals(exporter.lastError)){exporter.lastError=e.toString();GoldCraft.LOGGER.warn("Animated atlas export failed",e);}
        }finally {Performance.end("atlasAnimationCopy",began);}
    }
    public static void blockChanged(ClientWorld world,BlockPos pos) {
        WorldExporter exporter=active;
        if(exporter==null || exporter.sourceWorld!=world) return;
        exporter.changed.add(new Section(pos.getX()>>4,pos.getY()>>4,pos.getZ()>>4));
        // Face culling depends on neighbors across section boundaries.
        for(var direction:net.minecraft.util.math.Direction.values()) {
            BlockPos adjacent=pos.offset(direction);
            exporter.changed.add(new Section(adjacent.getX()>>4,adjacent.getY()>>4,adjacent.getZ()>>4));
        }
    }
    public void renderFrame() {
        long began=Performance.begin();
        try {renderFrameInternal();}finally{Performance.end("worldExport",began);}
    }
    private void renderFrameInternal() {
        MinecraftClient client=MinecraftClient.getInstance();
        if(!link.connected() || worldEpoch==0 || client.world==null || client.player==null) return;
        if(sourceWorld!=client.world) {
            if(!link.send(Wire.SCENE_RESET,new Wire.Writer().i64(worldEpoch).toByteArray()))return;
            dirty.clear(); changed.clear(); sent.clear();lamps.clear();entities.reset();particles.reset();sourceWorld=client.world; atlasSent=false; lastRefresh=lastRescan=0;
        }
        try {
            long now=System.nanoTime();
            if(!atlasSent)atlasSent=exportAtlas(client);
            if(!atlasSent) return;
            if(animations.dirty()&&link.send(Wire.ATLAS_PATCHES,animations.snapshot(worldEpoch,atlasGeneration)))animations.sent();
            if(now-lastRefresh>1_000_000_000L) { refresh(client); lastRefresh=now; }
            if(now-lastRescan>10_000_000_000L){dirty.addAll(sent.keySet());lastRescan=now;}
            long deadline=System.nanoTime()+4_000_000L;
            for(int processed=0;processed<8&&System.nanoTime()<deadline;processed++) {
                LinkedHashSet<Section> queue=changed.isEmpty()?dirty:changed;
                Iterator<Section> iterator=queue.iterator();
                if(!iterator.hasNext())break;
                Section section=iterator.next();iterator.remove();dirty.remove(section);exportSection(client,section);
            }
            if(now-lastLights>100_000_000L){sendLights(client);lastLights=now;}
        } catch(RuntimeException e) {
            if(!Objects.equals(e.toString(),lastError)) { lastError=e.toString(); GoldCraft.LOGGER.warn("World mesh export failed: {}",lastError); }
        }
    }
    private void refresh(MinecraftClient client) {
        BlockPos p=client.player.getBlockPos(); int sx=p.getX()>>4,sy=p.getY()>>4,sz=p.getZ()>>4;
        // Expand this radius after profiling the x86 host; startup remains bounded.
        Set<Section> wanted=new HashSet<>();
        for(int x=sx-2;x<=sx+2;x++) for(int z=sz-2;z<=sz+2;z++) for(int y=sy-2;y<=sy+2;y++) {
            if(y*16<sourceWorld.getBottomY() || y*16>=sourceWorld.getTopY()) continue;
            Section section=new Section(x,y,z); wanted.add(section); if(!sent.containsKey(section))dirty.add(section);
        }
        for(Section old:new ArrayList<>(sent.keySet())) if(!wanted.contains(old)) {
            if(link.send(Wire.REMOVE_SECTION,new Wire.Writer().i64(worldEpoch).i32(old.x).i32(old.y).i32(old.z).toByteArray())) {sent.remove(old);lamps.remove(old);}
        }
        dirty.removeIf(section->!wanted.contains(section));
        changed.removeIf(section->!wanted.contains(section));
    }
    private boolean exportAtlas(MinecraftClient client) {
        RenderSystem.assertOnRenderThread();
        int oldUnit=GL11.glGetInteger(GL13.GL_ACTIVE_TEXTURE);
        GL13.glActiveTexture(GL13.GL_TEXTURE0);
        int oldTexture=GL11.glGetInteger(GL11.GL_TEXTURE_BINDING_2D);
        try {
            var atlas=client.getBakedModelManager().getAtlas(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE);
            GL11.glBindTexture(GL11.GL_TEXTURE_2D,atlas.getGlId());
            int width=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_WIDTH);
            int height=GL11.glGetTexLevelParameteri(GL11.GL_TEXTURE_2D,0,GL11.GL_TEXTURE_HEIGHT);
            long size=(long)width*height*4;
            if(width<=0 || height<=0 || size>Wire.MAX_PAYLOAD-24) throw new IllegalArgumentException("Block atlas exceeds current transport limit");
            ByteBuffer pixels=BufferUtils.createByteBuffer((int)size);
            GL11.glGetTexImage(GL11.GL_TEXTURE_2D,0,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,pixels);
            byte[] bytes=new byte[(int)size]; pixels.get(bytes);
            long nextGeneration=atlasGeneration+1;
            boolean sent=link.send(Wire.ATLAS,new Wire.Writer().i64(worldEpoch).i64(nextGeneration).i32(width).i32(height).bytes(bytes).toByteArray());
            if(sent){atlasGeneration=nextGeneration;sourceAtlas=atlas;animations.reset(width,height);}
            return sent;
        } finally { GL11.glBindTexture(GL11.GL_TEXTURE_2D,oldTexture); GL13.glActiveTexture(oldUnit); }
    }
    private void exportSection(MinecraftClient client,Section section) {
        if(!sourceWorld.isChunkLoaded(section.x,section.z)) return;
        var chunk=sourceWorld.getChunk(section.x,section.z);
        int sectionIndex=sourceWorld.sectionCoordToIndex(section.y);
        if(sectionIndex<0 || sectionIndex>=chunk.getSectionArray().length) return;
        // Bit 0 is translucency; bit 1 marks self-emissive geometry. Keeping
        // emission separate prevents a point light from shadowing itself.
        Collector[] layers={new Collector(),new Collector(),new Collector(),new Collector()};
        List<Lamp> sectionLamps=new ArrayList<>();
        if(!chunk.getSection(sectionIndex).isEmpty()) {
            MatrixStack matrices=new MatrixStack(); Random random=Random.create(0);
            BlockPos.Mutable pos=new BlockPos.Mutable(); var renderer=client.getBlockRenderManager();
            for(int y=0;y<16;y++) for(int z=0;z<16;z++) for(int x=0;x<16;x++) {
                pos.set(section.x*16+x,section.y*16+y,section.z*16+z);
                var state=sourceWorld.getBlockState(pos);
                if(state.isAir()) continue;
                if(state.getLuminance()>0&&sectionLamps.size()<512) {
                    String id=net.minecraft.registry.Registries.BLOCK.getId(state.getBlock()).getPath();
                    int color=id.contains("soul")?0xffffdd99:id.contains("sea_lantern")?0xffffeed0:id.contains("redstone")?0xff6688ff:0xff9cd6ff;
                    sectionLamps.add(new Lamp(pos.getX()+0.5f,pos.getY()+0.6f,pos.getZ()+0.5f,state.getLuminance()*32.0f,color));
                }
                if(state.getRenderType()==BlockRenderType.MODEL) {
                    int layer=(RenderLayers.getBlockLayer(state)==RenderLayer.getTranslucent()?1:0)|(state.getLuminance()>0?2:0);
                    Collector target=layers[layer];
                    target.offset(0,0,0); matrices.push(); matrices.translate(pos.getX(),pos.getY(),pos.getZ());
                    renderer.renderBlock(state,pos,sourceWorld,matrices,target,true,random); matrices.pop();
                }
                if(!state.getFluidState().isEmpty()) {
                    int layer=(RenderLayers.getFluidLayer(state.getFluidState())==RenderLayer.getTranslucent()?1:0)|(state.getLuminance()>0?2:0);
                    Collector target=layers[layer];
                    target.offset(section.x*16,section.y*16,section.z*16);
                    renderer.renderFluid(pos,sourceWorld,target,state,state.getFluidState());
                }
            }
        }
        lamps.put(section,sectionLamps);
        byte[][] mesh=Arrays.stream(layers).map(Collector::finish).toArray(byte[][]::new);
        byte[][] previous=sent.get(section);
        boolean allSent=true;
        for(int layer=0;layer<layers.length;layer++) {
            if(previous!=null && Arrays.equals(previous[layer],mesh[layer])) continue;
            byte[] payload=new Wire.Writer().i64(worldEpoch).i32(section.x).i32(section.y).i32(section.z)
                .i64(++revision).i32(layer).i32(mesh[layer].length/24).bytes(mesh[layer]).toByteArray();
            if(!link.send(Wire.SECTION_MESH,payload)) allSent=false;
        }
        if(allSent) sent.put(section,mesh); else dirty.add(section);
    }

    private void sendLights(MinecraftClient client) {
        var position=client.player.getPos();
        List<Lamp> nearby=lamps.values().stream().flatMap(List::stream).filter(l->position.squaredDistanceTo(l.x,l.y,l.z)<24*24)
            .sorted(Comparator.comparingDouble(l->position.squaredDistanceTo(l.x,l.y,l.z))).limit(12).toList();
        Wire.Writer w=new Wire.Writer().i64(worldEpoch).i32(nearby.size());
        for(Lamp l:nearby)w.f32(l.x).f32(l.y).f32(l.z).f32(l.radius).i32(l.color);
        link.send(Wire.LIGHTS,w.toByteArray());
    }
    static final class Collector implements VertexConsumer {
        private final Wire.Writer writer=new Wire.Writer();
        private float x,y,z,u,v,offsetX,offsetY,offsetZ;
        private int red=255,green=255,blue=255,alpha=255,count,sourceCount,overlayColor;
        private final boolean withOverlay,triangles;
        private int hurtVertices;
        private boolean pending;
        Collector(){this(false,false);}
        Collector(boolean withOverlay,boolean triangles){this.withOverlay=withOverlay;this.triangles=triangles;}
        int hurtVertices(){return hurtVertices;}
        void offset(float x,float y,float z) { offsetX=x;offsetY=y;offsetZ=z; }
        private void flush() {
            if(!pending) return;
            sourceCount++;
            writeVertex();
            // A repeated final triangle vertex produces one valid and one degenerate
            // indexed triangle, preserving Epic Fight's triangulated mesh topology.
            if(triangles&&sourceCount%3==0)writeVertex();
            pending=false;
        }
        private void writeVertex(){
            if(++count>262144)throw new IllegalArgumentException("Section vertex budget exceeded");
            writer.f32(x).f32(y).f32(z).f32(u).f32(v).u8(red).u8(green).u8(blue).u8(alpha);
            if(withOverlay){writer.i32(overlayColor);if(overlayColor==0x4d0000ff)hurtVertices++;}
        }
        byte[] finish() { flush(); if(sourceCount%(triangles?3:4)!=0) throw new IllegalArgumentException("Incomplete model primitive"); return writer.toByteArray(); }
        @Override public VertexConsumer vertex(float x,float y,float z) { flush(); this.x=x+offsetX;this.y=y+offsetY;this.z=z+offsetZ;overlayColor=0;pending=true;return this; }
        @Override public VertexConsumer color(int red,int green,int blue,int alpha) {this.red=red;this.green=green;this.blue=blue;this.alpha=alpha;return this;}
        @Override public VertexConsumer texture(float u,float v) {this.u=u;this.v=v;return this;}
        @Override public VertexConsumer overlay(int u,int v) {overlayColor=dev.goldcraft.bridge.OverlayColor.fromUv(u,v);return this;}
        @Override public VertexConsumer light(int u,int v) {return this;}
        @Override public VertexConsumer normal(float x,float y,float z) {return this;}
    }
}
