package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.registry.tag.FluidTags;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.shape.VoxelShapes;
import net.minecraft.util.function.BooleanBiFunction;
import net.minecraft.world.chunk.WorldChunk;
import java.util.*;

/** Server-owned liquid volumes, independent of solid proxies and edict slots. */
public final class MinecraftFluids {
    public static final int LIMIT=8192,WATER=-3,LAVA=-5;
    public record Volume(int contents,Box box) {}
    private static ServerWorld world;
    private static long epoch,revision,lastSentTick,geometryRevision;
    private static final Map<Long,WorldChunk> dirty=new LinkedHashMap<>();
    private static final Map<Long,List<Volume>> chunks=new TreeMap<>();
    private static final Set<Long> geometryDirty=new HashSet<>();
    private static List<Volume> published=List.of();
    private static byte[] packet;
    private static int overflow;
    private MinecraftFluids(){}
    public static void initialize(){
        var bus=net.neoforged.neoforge.common.NeoForge.EVENT_BUS;
        bus.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Load event)->{
            if(event.getLevel()==world&&event.getChunk() instanceof WorldChunk c)changed(c);
        });
        bus.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Unload event)->{
            if(event.getLevel()==world){long key=event.getChunk().getPos().toLong();dirty.remove(key);chunks.remove(key);}
        });
    }
    public static void changed(WorldChunk chunk){if(chunk.getWorld()==world&&!HostWorldReset.clearing())dirty.put(chunk.getPos().toLong(),chunk);}
    /** A block-only round reset keeps the native map and snapshot revision. */
    public static void invalidateBlocks(ServerWorld changedWorld){
        if(world!=changedWorld)return;
        dirty.clear();chunks.clear();geometryDirty.clear();
        published=List.of();packet=null;overflow=0;
        // CLEARING suppresses per-block change callbacks. Rebuild from the
        // cleared chunks and publish a new revision, including an empty field.
        // clear() would reset the revision within the same authoritative epoch.
        for(var chunk:HostWorldReset.loadedChunks(world))dirty.put(chunk.getPos().toLong(),chunk);
    }
    public static void clear(){world=null;epoch=revision=lastSentTick=geometryRevision=0;dirty.clear();chunks.clear();geometryDirty.clear();published=List.of();packet=null;overflow=0;}
    public static List<Volume> scan(WorldChunk chunk){
        return scan(chunk,false);
    }
    private static List<Volume> scan(WorldChunk chunk,boolean reschedule){
        var view=chunk.getWorld();var result=new ArrayList<Volume>();var sections=chunk.getSectionArray();
        int sx=chunk.getPos().getStartX(),sz=chunk.getPos().getStartZ();
        for(int sectionIndex=0;sectionIndex<sections.length;sectionIndex++){
            var section=sections[sectionIndex];if(section.isEmpty())continue;int sy=chunk.getBottomY()+sectionIndex*16;
            for(int y=0;y<16;y++)for(int z=0;z<16;z++)for(int x=0;x<16;x++){
                var state=section.getBlockState(x,y,z);var fluid=state.getFluidState();
                int contents=fluid.isIn(FluidTags.WATER)?WATER:fluid.isIn(FluidTags.LAVA)?LAVA:0;if(contents==0)continue;
                var pos=new BlockPos(sx+x,sy+y,sz+z);
                if(reschedule&&view instanceof ServerWorld server)server.scheduleFluidTick(pos,fluid.getFluid(),1);
                // The same conservative host cells used by fluid propagation.
                // Legacy spilled liquid inside the BSP never becomes playable water.
                if(!HostCollision.shape(view,pos).isEmpty()){
                    if(state.getBlock() instanceof net.minecraft.block.FluidBlock&&view instanceof ServerWorld server)
                        server.scheduleFluidTick(pos,fluid.getFluid(),1);
                    continue;
                }
                double height=fluid.getHeight(view,pos);if(height<=0)continue;
                var shape=VoxelShapes.cuboid(0,0,0,1,height,1);
                var solid=state.getCollisionShape(view,pos);
                if(!solid.isEmpty())shape=VoxelShapes.combineAndSimplify(shape,solid,BooleanBiFunction.ONLY_FIRST);
                for(var box:shape.getBoundingBoxes())result.add(new Volume(contents,box.offset(pos)));
            }
        }
        return merge(result);
    }
    private static double min(Box b,int axis){return axis==0?b.minX:axis==1?b.minY:b.minZ;}
    private static double max(Box b,int axis){return axis==0?b.maxX:axis==1?b.maxY:b.maxZ;}
    /** Coalesce equal-height adjacent cells before crossing the native bridge. */
    public static List<Volume> merge(List<Volume> input){
        var values=new ArrayList<>(input);
        for(int axis=0;axis<3;axis++){
            final int a=axis,b=(axis+1)%3,c=(axis+2)%3;
            values.sort(Comparator.comparingInt(Volume::contents)
                .thenComparingDouble(v->min(v.box,b)).thenComparingDouble(v->max(v.box,b))
                .thenComparingDouble(v->min(v.box,c)).thenComparingDouble(v->max(v.box,c))
                .thenComparingDouble(v->min(v.box,a)));
            var next=new ArrayList<Volume>();
            for(var v:values){
                if(!next.isEmpty()){
                    var old=next.getLast();var x=old.box;var y=v.box;
                    if(old.contents==v.contents&&min(x,b)==min(y,b)&&max(x,b)==max(y,b)
                        &&min(x,c)==min(y,c)&&max(x,c)==max(y,c)&&max(x,a)==min(y,a)){
                        next.set(next.size()-1,new Volume(v.contents,x.union(y)));continue;
                    }
                }
                next.add(v);
            }
            values=next;
        }
        return List.copyOf(values);
    }
    public static byte[] encode(long epoch,long revision,List<Volume> volumes){
        if(epoch==0||revision==0||volumes.size()>LIMIT)throw new IllegalArgumentException("Fluid snapshot bounds");
        var w=new Wire.Writer().i64(epoch).i64(revision).i32(volumes.size());
        for(var volume:volumes){var box=volume.box;
            w.i32(volume.contents).f32((float)(box.minX*32)).f32((float)(-box.maxZ*32)).f32((float)((box.minY-64)*32))
                .f32((float)(box.maxX*32)).f32((float)(-box.minZ*32)).f32((float)((box.maxY-64)*32));
        }
        return w.toByteArray();
    }
    public static void tick(MinecraftServer server,HostWorldState host){
        var next=HostSession.hostWorld(server,host);if(next==null)return;
        if(world!=next||epoch!=host.epoch()){
            clear();world=next;epoch=host.epoch();for(var chunk:HostWorldReset.loadedChunks(world))changed(chunk);
        }
        long geometry=HostCollision.revision(world);
        if(geometry!=geometryRevision){
            geometryRevision=geometry;
            for(var chunk:HostWorldReset.loadedChunks(world)){
                long key=chunk.getPos().toLong();var existing=chunks.get(key);
                if(existing!=null&&!existing.isEmpty()){geometryDirty.add(key);changed(chunk);}
            }
        }
        for(int i=0;i<8&&!dirty.isEmpty();i++){
            var it=dirty.entrySet().iterator();var chunk=it.next().getValue();it.remove();long key=chunk.getPos().toLong();
            chunks.put(key,scan(chunk,geometryDirty.remove(key)));
        }
        if(!GoldCraft.fluidsAvailable())return;
        var actors=host.actors();var selected=new ArrayList<Volume>();
        for(var chunk:chunks.values())for(var volume:chunk){
            var b=volume.box;
            // Native engine coordinates are bounded. No clipping or silent
            // truncation: volumes outside its world cannot affect host actors.
            if(b.minX< -512||b.maxX>512||b.minZ< -512||b.maxZ>512||b.minY< -448||b.maxY>576)continue;
            boolean nearby=false;
            for(var actor:actors)if((actor.flags()&1)!=0){var p=actor.minecraftFeet();
                if(p.x()>b.minX-32&&p.x()<b.maxX+32&&p.y()>b.minY-32&&p.y()<b.maxY+32&&p.z()>b.minZ-32&&p.z()<b.maxZ+32){nearby=true;break;}
            }
            if(nearby)selected.add(volume);
        }
        int excess=Math.max(0,selected.size()-LIMIT);
        if(excess>0){if(overflow==0)GoldCraft.LOGGER.error("Fluid snapshot exceeds {} volumes; withholding incomplete authority",LIMIT);overflow=excess;return;}
        overflow=0;
        if(packet==null||!published.equals(selected)){published=List.copyOf(selected);packet=encode(epoch,++revision,published);lastSentTick=Long.MIN_VALUE;}
        long time=world.getTime();
        if(lastSentTick==Long.MIN_VALUE||time-lastSentTick>=5)
            if(GoldCraft.sendFluidsToHost(packet))lastSentTick=time;
    }
    public static void diagnostics(com.google.gson.JsonObject data){
        var f=new com.google.gson.JsonObject();f.addProperty("revision",revision);f.addProperty("volumes",published.size());
        f.addProperty("pendingChunks",dirty.size());f.addProperty("overflow",overflow);data.add("minecraftFluids",f);
    }
}
