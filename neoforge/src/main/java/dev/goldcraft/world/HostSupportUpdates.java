package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import net.minecraft.world.chunk.WorldChunk;
import java.util.*;

/** A changed BSP/door has no vanilla block event. Re-run vanilla neighbour
 * transitions in bounded batches, retaining normal drops and falling ticks. */
public final class HostSupportUpdates {
    private static final Map<ServerWorld,Tracker> WORLDS=new IdentityHashMap<>();
    private static final class Tracker {
        long epoch,revision=-1;
        final Map<Long,Set<BlockPos>> blocks=new HashMap<>();
        final Map<Long,WorldChunk> scans=new LinkedHashMap<>();
        final ArrayDeque<BlockPos> pending=new ArrayDeque<>();
    }
    private HostSupportUpdates(){}
    public static void initialize(){
        var bus=net.neoforged.neoforge.common.NeoForge.EVENT_BUS;
        bus.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Load event)->{
            if(event.getLevel() instanceof ServerWorld world&&event.getChunk() instanceof WorldChunk chunk){
                var t=WORLDS.get(world);if(t!=null)t.scans.put(chunk.getPos().toLong(),chunk);
            }
        });
        bus.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Unload event)->{
            if(event.getLevel() instanceof ServerWorld world){
                var t=WORLDS.get(world);if(t!=null){long key=event.getChunk().getPos().toLong();t.scans.remove(key);t.blocks.remove(key);}
            }
        });
        bus.addListener((net.neoforged.neoforge.event.server.ServerStoppedEvent event)->WORLDS.clear());
    }
    public static void attach(World view){
        if(!(view instanceof ServerWorld world))return;
        var t=new Tracker();var host=HostCollision.state(world);t.epoch=host==null?0:host.epoch();WORLDS.put(world,t);
        for(var chunk:HostWorldReset.loadedChunks(world))t.scans.put(chunk.getPos().toLong(),chunk);
    }
    public static void changed(WorldChunk chunk,BlockPos pos,BlockState state){
        if(!(chunk.getWorld() instanceof ServerWorld world))return;
        var t=WORLDS.get(world);if(t==null)return;
        long key=chunk.getPos().toLong();
        if(state.isAir()){
            var positions=t.blocks.get(key);if(positions!=null){positions.remove(pos);if(positions.isEmpty())t.blocks.remove(key);}
        }else t.blocks.computeIfAbsent(key,ignored->new HashSet<>()).add(pos.toImmutable());
    }
    private static void scan(Tracker t,WorldChunk chunk){
        var positions=new HashSet<BlockPos>();var sections=chunk.getSectionArray();
        for(int i=0;i<sections.length;i++){
            var section=sections[i];if(section.isEmpty())continue;
            int sy=chunk.getBottomY()+16*i;
            for(int y=0;y<16;y++)for(int z=0;z<16;z++)for(int x=0;x<16;x++)
                if(!section.getBlockState(x,y,z).isAir())positions.add(new BlockPos(chunk.getPos().getStartX()+x,sy+y,chunk.getPos().getStartZ()+z));
        }
        t.blocks.put(chunk.getPos().toLong(),positions);t.pending.addAll(positions);
    }
    public static void tick(MinecraftServer server,HostWorldState host){
        var world=HostSession.hostWorld(server,host);if(world!=null)tick(world);
    }
    public static void tick(ServerWorld world){
        var host=HostCollision.state(world);if(host==null||host.geometry()==null)return;
        var t=WORLDS.get(world);
        if(t==null||t.epoch!=host.epoch()){attach(world);t=WORLDS.get(world);}
        if(!t.scans.isEmpty()){
            var entry=t.scans.entrySet().iterator().next();t.scans.remove(entry.getKey());scan(t,entry.getValue());
        }
        long revision=HostCollision.revision(world);
        // Finish a batch even if a moving door publishes every tick; restarting
        // the queue would starve the later blocks forever.
        if(t.pending.isEmpty()&&t.revision!=revision){
            t.revision=revision;for(var positions:t.blocks.values())t.pending.addAll(positions);
        }
        for(int count=0;count<256&&!t.pending.isEmpty();count++){
            var pos=t.pending.remove();if(!world.isChunkLoaded(pos))continue;
            var state=world.getBlockState(pos);
            if(state.isAir()){
                var positions=t.blocks.get(net.minecraft.util.math.ChunkPos.toLong(pos.getX()>>4,pos.getZ()>>4));
                if(positions!=null)positions.remove(pos);
                continue;
            }
            Block.replace(state,Block.postProcessState(state,world,pos),world,pos,Block.NOTIFY_ALL);
        }
    }
}
