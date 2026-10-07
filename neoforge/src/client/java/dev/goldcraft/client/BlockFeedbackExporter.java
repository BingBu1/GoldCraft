package dev.goldcraft.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.FramePacer;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.client.mixin.WorldRendererAccessor;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.block.BlockRenderType;
import net.minecraft.block.ShapeContext;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.OverlayVertexConsumer;
import net.minecraft.client.texture.SpriteAtlasTexture;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.util.Identifier;
import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.hit.HitResult;
import net.minecraft.util.math.BlockPos;

/** Exports WorldRenderer's real target shape, destruction progress and model geometry. */
final class BlockFeedbackExporter {
    private static BlockFeedbackExporter active;
    private final BridgeLink link;
    private final FramePacer pacing=new FramePacer(60);
    private long revision,frames;
    private int edges,cracks;
    private JsonArray stages=new JsonArray();
    private String error="";
    BlockFeedbackExporter(BridgeLink link){this.link=link;active=this;}
    static void diagnostics(JsonObject data){
        if(active==null)return;
        JsonObject value=new JsonObject();value.addProperty("frames",active.frames);value.addProperty("outlineEdges",active.edges);
        value.addProperty("breakingBlocks",active.cracks);value.add("stages",active.stages);value.addProperty("error",active.error);data.add("blockFeedback",value);
    }
    void frame(long epoch){
        var client=MinecraftClient.getInstance();
        if(!link.connected()||!HostInput.hosted()||client.world==null||client.player==null||!pacing.ready(System.nanoTime()))return;
        try {
            Wire.Writer lines=new Wire.Writer();int[] lineCount={0};
            if(HostInput.controlling()&&client.currentScreen==null&&!client.options.hudHidden
                    &&client.crosshairTarget instanceof BlockHitResult hit&&hit.getType()==HitResult.Type.BLOCK){
                BlockPos pos=hit.getBlockPos();var state=client.world.getBlockState(pos);
                if(!state.isAir()&&client.world.getWorldBorder().contains(pos))
                    state.getOutlineShape(client.world,pos,ShapeContext.of(client.player)).forEachEdge((x1,y1,z1,x2,y2,z2)->{
                        if(lineCount[0]>=2048)throw new IllegalArgumentException("Block outline budget exceeded");
                        for(double[] p:new double[][]{{x1,y1,z1},{x2,y2,z2}})
                            lines.f32((float)(pos.getX()+p[0])).f32((float)(pos.getY()+p[1])).f32((float)(pos.getZ()+p[2]));
                        lineCount[0]+=2;
                    });
            }
            record Crack(int stage,byte[] vertices){}
            List<Crack> batches=new ArrayList<>();int vertices=0;JsonArray nextStages=new JsonArray();
            var progress=((WorldRendererAccessor)client.worldRenderer).goldcraft$breakingProgress();
            for(var entry:progress.long2ObjectEntrySet()){
                if(batches.size()>=32)break;
                BlockPos pos=BlockPos.fromLong(entry.getLongKey());var set=entry.getValue();
                if(set==null||set.isEmpty()||pos.getSquaredDistance(client.player.getPos())>32*32)continue;
                int stage=set.last().getStage();if(stage<0||stage>9)continue;
                var state=client.world.getBlockState(pos);if(state.isAir())continue;
                MatrixStack matrices=new MatrixStack();matrices.translate(pos.getX(),pos.getY(),pos.getZ());
                var sprite=client.getBakedModelManager().getAtlas(SpriteAtlasTexture.BLOCK_ATLAS_TEXTURE)
                    .getSprite(Identifier.ofVanilla("block/destroy_stage_"+stage));
                var output=new WorldExporter.Collector();
                var overlay=new OverlayVertexConsumer(sprite.getTextureSpecificVertexConsumer(output),matrices.peek(),1.0f);
                client.getBlockRenderManager().renderBreakingTexture(state,pos,client.world,matrices,overlay,client.world.getModelData(pos));
                if(state.getRenderType()==BlockRenderType.ENTITYBLOCK_ANIMATED){
                    var blockEntity=client.world.getBlockEntity(pos);
                    if(blockEntity!=null)client.getBlockEntityRenderDispatcher().render(blockEntity,client.getRenderTickCounter().getTickDelta(false),matrices,
                        layer->layer.hasCrumbling()?overlay:new WorldExporter.Collector());
                }
                byte[] mesh=output.finish();if(mesh.length==0)continue;
                vertices+=mesh.length/24;if(vertices>65536)throw new IllegalArgumentException("Block crack budget exceeded");
                batches.add(new Crack(stage,mesh));nextStages.add(stage);
            }
            Wire.Writer packet=new Wire.Writer().i64(epoch).i64(++revision).i32(HostInput.life()).i32(lineCount[0]).bytes(lines.toByteArray()).i32(batches.size());
            for(var batch:batches)packet.i32(batch.stage()).i32(batch.vertices().length/24).bytes(batch.vertices());
            if(link.send(Wire.BLOCK_FEEDBACK,packet.toByteArray())){frames++;edges=lineCount[0]/2;cracks=batches.size();stages=nextStages;error="";}
        }catch(RuntimeException failure){if(!failure.toString().equals(error)){error=failure.toString();GoldCraft.LOGGER.warn("Block feedback export failed",failure);}}
    }
}
