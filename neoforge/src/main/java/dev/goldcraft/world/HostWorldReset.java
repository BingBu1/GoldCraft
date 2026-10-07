package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.mixin.ChunkResetAccessor;
import net.minecraft.block.Blocks;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockBox;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.chunk.WorldChunk;
import net.minecraft.world.tick.ChunkTickScheduler;
import java.util.*;

/** Buildings belong to one authoritative CS map epoch, never to a client connection. */
public final class HostWorldReset {
    private static final Map<ServerWorld,Session> WORLDS=new IdentityHashMap<>();
    private static final ThreadLocal<Boolean> CLEARING=ThreadLocal.withInitial(()->false);
    private static final class Session {
        long epoch,removed;
        final Map<Long,WorldChunk> loaded=new HashMap<>();
        final Set<Long> prepared=new HashSet<>();
    }
    private HostWorldReset(){}
    public static boolean clearing(){return CLEARING.get();}
    public static Collection<WorldChunk> loadedChunks(ServerWorld world){
        var session=WORLDS.get(world);return session==null?List.of():List.copyOf(session.loaded.values());
    }
    private static boolean managed(ServerWorld world) {
        var id=world.getRegistryKey().getValue();
        return id.getNamespace().equals("goldcraft")&&id.getPath().matches("[a-z0-9_]+_[0-9a-f]{8}");
    }
    public static void initialize() {
        net.neoforged.neoforge.common.NeoForge.EVENT_BUS.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Load event)-> {
            if(!(event.getLevel() instanceof ServerWorld world)||!(event.getChunk() instanceof WorldChunk chunk))return;
            if(!managed(world))return;
            var session=WORLDS.computeIfAbsent(world,ignored->new Session());
            session.loaded.put(chunk.getPos().toLong(),chunk);
            if(session.epoch!=0)prepare(world,session,chunk);
        });
        net.neoforged.neoforge.common.NeoForge.EVENT_BUS.addListener((net.neoforged.neoforge.event.level.ChunkEvent.Unload event)-> {
            if(!(event.getLevel() instanceof ServerWorld world)||!(event.getChunk() instanceof WorldChunk chunk))return;
            var session=WORLDS.get(world);
            if(session!=null)session.loaded.remove(chunk.getPos().toLong(),chunk);
        });
        net.neoforged.neoforge.common.NeoForge.EVENT_BUS.addListener((net.neoforged.neoforge.event.server.ServerStoppedEvent event)->WORLDS.clear());
    }
    public static void activate(ServerWorld world,long epoch) {
        if(epoch==0||!managed(world))throw new IllegalArgumentException("Invalid transient host dimension");
        var session=WORLDS.computeIfAbsent(world,ignored->new Session());
        if(session.epoch==epoch)return;
        session.epoch=epoch;session.removed=0;session.prepared.clear();
        long started=System.nanoTime();
        // Unloaded chunks are cleared when loaded. Remember prepared chunks so that
        // unloading/reloading in this same map session does not erase new buildings.
        for(var chunk:List.copyOf(session.loaded.values()))prepare(world,session,chunk);
        GoldCraft.LOGGER.info("Fresh host map session: dimension={} epoch={} clearedBlocks={} loadedChunks={} elapsedMs={}",
            world.getRegistryKey().getValue(),Long.toUnsignedString(epoch),session.removed,session.loaded.size(),(System.nanoTime()-started)/1_000_000.0);
    }
    private static void prepare(ServerWorld world,Session session,WorldChunk chunk) {
        if(!session.prepared.add(chunk.getPos().toLong()))return;
        boolean previous=CLEARING.get();CLEARING.set(true);
        try {
            // Remove containers without spilling their inventory into the next session.
            for(var pos:chunk.getBlockEntityPositions())chunk.removeBlockEntity(pos);
            ((ChunkResetAccessor)chunk).goldcraft$pendingBlockEntities().clear();
            var air=Blocks.AIR.getDefaultState();var position=new BlockPos.Mutable();
            var sections=chunk.getSectionArray();int startX=chunk.getPos().getStartX(),startZ=chunk.getPos().getStartZ();
            for(int sectionIndex=sections.length-1;sectionIndex>=0;sectionIndex--) {
                var section=sections[sectionIndex];if(section.isEmpty())continue;
                int bottom=chunk.getBottomY()+sectionIndex*16;
                for(int y=15;y>=0;y--)for(int z=0;z<16;z++)for(int x=0;x<16;x++) {
                    if(section.getBlockState(x,y,z).isAir())continue;
                    position.set(startX+x,bottom+y,startZ+z);
                    // Keep vanilla heightmap/light bookkeeping, suppressing only block
                    // replacement callbacks that would drop items or alter neighbours.
                    chunk.setBlockState(position,air,false);
                    world.getChunkManager().markForUpdate(position);session.removed++;
                }
            }
            // Saved ticks may still be relative when the chunk first becomes FULL.
            ((ChunkTickScheduler<?>)chunk.getBlockTickScheduler()).disable(world.getTime());
            ((ChunkTickScheduler<?>)chunk.getFluidTickScheduler()).disable(world.getTime());
            var bounds=new BlockBox(startX,world.getBottomY(),startZ,startX+15,world.getTopY()-1,startZ+15);
            world.getBlockTickScheduler().clearNextTicks(bounds);world.getFluidTickScheduler().clearNextTicks(bounds);
            chunk.setNeedsSaving(true);
        }finally{CLEARING.set(previous);}
    }
}
