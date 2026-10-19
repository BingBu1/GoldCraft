package dev.goldcraft.cacheprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapEdits;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.BspMap;
import dev.goldcraft.world.HostCollision;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.phys.shapes.VoxelShape;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;
import java.util.List;
import java.util.Map;

/** Real production FML collision cache; authenticated edit messages are fixture inputs. */
@Mod("goldcraft_carved_cache_probe")
public final class CarvedCacheProbe {
    private static int checks;
    private static void check(boolean value,String message){checks++;if(!value)throw new AssertionError(message);}
    public CarvedCacheProbe(){NeoForge.EVENT_BUS.addListener(CarvedCacheProbe::run);}
    private static Object field(Object object,String name)throws Exception {
        var f=object.getClass().getDeclaredField(name);f.setAccessible(true);return f.get(object);
    }
    @SuppressWarnings("unchecked")
    private static Map<Long,VoxelShape> shapes(ServerLevel level,String name)throws Exception {
        var f=HostCollision.class.getDeclaredField("WORLDS");f.setAccessible(true);
        return (Map<Long,VoxelShape>)field(((Map<?,?>)f.get(null)).get(level),name);
    }
    private static void load(HostWorldState host,byte[] bsp,long epoch)throws Exception {
        host.world(new Wire.Writer().i64(epoch).string("cs_assault").toByteArray());
        host.bsp(new Wire.Writer().i64(epoch).i32((int)BspMap.checksum(bsp)).i32(bsp.length).bytes(bsp).toByteArray());
        var f=HostWorldState.class.getDeclaredField("dimension");f.setAccessible(true);f.set(host,"minecraft:overworld");
    }
    private static void exercise(ServerLevel world)throws Exception {
        var bsp=Files.readAllBytes(FMLPaths.GAMEDIR.get().resolve("cs_assault.bsp"));
        var host=new HostWorldState();load(host,bsp,71);HostCollision.attach(world,host);
        var near=new BlockPos(2,76,-91);var far=new BlockPos(4,76,-91);
        check(!HostCollision.shape(world,near).isEmpty(),"wall originally solid");
        for(int x=-6;x<18;x++)HostCollision.shape(world,new BlockPos(x,76,-91));
        var fixed=shapes(world,"staticShapes");var moving=shapes(world,"movingShapes");
        var before=Map.copyOf(fixed);var movingBefore=Map.copyOf(moving);
        long revision=HostCollision.revision(world);
        var target=new MapEdits.Target(0,0,0);
        var first=new MapEdits.Cut(2,target,new MapEdits.Box(64,2864,358.4f,96,2960,454.4f));
        var second=new MapEdits.Cut(3,target,new MapEdits.Box(96,2864,358.4f,128,2960,454.4f));
        host.edits().accept(new MapEdits.Snapshot(71,2,List.of(first)));
        check(HostCollision.revision(world)>revision,"append advances cache revision");
        for(var e:before.entrySet()) {
            // Each seeded cell is one block wide; only x=2 intersects this cut.
            if(BlockPos.of(e.getKey()).getX()==2)check(!fixed.containsKey(e.getKey()),"touched cell evicted");
            else check(fixed.get(e.getKey())==e.getValue(),"far/boundary cell retained");
        }
        check(moving.equals(movingBefore),"world edit retains moving cache");
        check(HostCollision.shape(world,near).isEmpty(),"new hole has no solid voxel");
        check(!HostCollision.shape(world,far).isEmpty(),"untouched wall remains solid");
        var prior=host.collision();
        host.edits().accept(new MapEdits.Snapshot(71,3,List.of(first,second)));
        check(host.collision().appendedChanges(prior)!=null,"exact prefix recognized");
        HostCollision.revision(world);
        check(fixed.get(far.asLong())==before.get(far.asLong()),"second hole retains far shape");
        check(HostCollision.shape(world,new BlockPos(3,76,-91)).isEmpty(),"second hole collision updated");
        var preserved=Map.copyOf(fixed);
        host.edits().accept(new MapEdits.Snapshot(71,4,List.of(first,second)));
        HostCollision.revision(world);check(fixed.equals(preserved),"no-op snapshot retains cache");
        var brush=new MapEdits.Cut(5,new MapEdits.Target(1,1,1),new MapEdits.Box(-1,-1,-1,1,1,1));
        host.edits().accept(new MapEdits.Snapshot(71,5,List.of(first,second,brush)));
        HostCollision.revision(world);
        check(fixed.equals(preserved),"brush append retains static cache");
        check(moving.isEmpty(),"brush append invalidates moving cache");
        HostCollision.shape(world,far);prior=host.collision();
        host.edits().accept(new MapEdits.Snapshot(71,6,List.of(
            new MapEdits.Cut(2,target,second.box()),new MapEdits.Cut(3,target,first.box()),brush)));
        check(host.collision().appendedChanges(prior)==null,"replacement requires full invalidation");
        HostCollision.revision(world);check(fixed.isEmpty()&&moving.isEmpty(),"replacement clears both caches");
        HostCollision.shape(world,near);HostCollision.shape(world,far);prior=host.collision();
        host.edits().accept(new MapEdits.Snapshot(71,7,List.of()));
        check(host.collision().appendedChanges(prior)==null,"restore requires full invalidation");
        HostCollision.revision(world);check(fixed.isEmpty()&&moving.isEmpty(),"restore clears both caches");
        check(!HostCollision.shape(world,near).isEmpty(),"restore makes wall solid again");
        prior=host.collision();load(host,bsp,72);
        check(host.collision().appendedChanges(prior)==null,"map identity requires full invalidation");
        HostCollision.revision(world);check(fixed.isEmpty()&&moving.isEmpty(),"map change clears both caches");
    }
    private static void run(ServerStartedEvent event) {
        var report=new JsonObject();
        try{exercise(event.getServer().overworld());report.addProperty("passed",true);}
        catch(Throwable t){report.addProperty("passed",false);report.addProperty("error",t.toString());t.printStackTrace();}
        finally {
            report.addProperty("checks",checks);
            try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("carved-cache.json"),report.toString());}
            catch(Exception e){throw new RuntimeException(e);}
        }
    }
}
