package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapMining;
import dev.goldcraft.bridge.Wire;
import net.minecraft.block.BlockState;
import net.minecraft.block.Blocks;
import net.minecraft.entity.TntEntity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.World;
import net.minecraft.world.explosion.Explosion;
import java.util.*;

/** Adds host geometry to the real vanilla explosion rays, without placing blocks. */
public final class HostExplosions {
    private static long event, materialEpoch;
    private static Map<String,Integer> materials=Map.of();
    private static BspMap parsedMap;
    private static BspSurfaceTextures surfaces;
    private HostExplosions() {}
    public static void clear() { event=materialEpoch=0;materials=Map.of();parsedMap=null;surfaces=null; }

    public static void materials(HostWorldState host,byte[] payload) {
        var r=new Wire.Reader(payload);long epoch=r.i64();int count=r.i32();
        if(epoch==0||count<0||count>4096)throw new IllegalArgumentException("Host material bounds");
        Map<String,Integer> next=new HashMap<>();
        for(int i=0;i<count;i++) {
            String name=r.string(16);int material=r.i32();
            if(name.isEmpty()||material<0||material>9||next.put(name,material)!=null)
                throw new IllegalArgumentException("Host material entry");
        }
        r.finish();if(epoch!=host.epoch())return;
        materialEpoch=epoch;materials=Map.copyOf(next);
    }

    public static Context begin(World world,Explosion explosion) {
        var host=GoldCraft.HOST_WORLD;
        // TNT is discarded before exploding; its removed flag is not a veto.
        if(world.isClient()||world.getServer()==null||!(explosion.getEntity() instanceof TntEntity)
            ||!explosion.shouldDestroy()||!GoldCraft.hostExplosionsAvailable()
            ||!world.getRegistryKey().getValue().toString().equals(host.dimension())
            ||host.epoch()==0||host.mining().mode()!=MapMining.ALL_GEOMETRY
            ||(host.mining().capabilities()&MapMining.GEOMETRY_CARVING)==0
            ||!host.edits().ready()||host.collision()==null||materialEpoch!=host.epoch())return null;
        Vec3d center=explosion.getPosition();float power=explosion.getPower();
        if(!HostExplosionPacket.validOrigin(center,power))return null;
        if(parsedMap!=host.geometry()) {
            parsedMap=host.geometry();surfaces=null;
            try { surfaces=new BspSurfaceTextures(host.bspBytes()); }
            catch(IllegalArgumentException invalid) { GoldCraft.LOGGER.warn("Host TNT disabled for this BSP: {}",invalid.getMessage()); }
        }
        return surfaces==null?null:new Context(host,center,power,surfaces,materials);
    }

    public static final class Context {
        private record Cell(BlockState state,boolean solid) {}
        private final HostWorldState host;
        private final long epoch,policy;
        private final Vec3d center;
        private final float power;
        private final BspSurfaceTextures textures;
        private final Map<String,Integer> catalog;
        private final Map<Long,Cell> cells=new HashMap<>();
        private final Set<BlockPos> reached=new HashSet<>();
        private final BspSurfaceTextures.Budget budget=new BspSurfaceTextures.Budget(2_000_000);
        private boolean committed;
        private Context(HostWorldState host,Vec3d center,float power,BspSurfaceTextures textures,Map<String,Integer> catalog) {
            this.host=host;epoch=host.epoch();policy=host.mining().revision();
            this.center=center;this.power=power;this.textures=textures;this.catalog=catalog;
        }
        private Cell cell(BlockPos pos) {
            Cell existing=cells.get(pos.asLong());if(existing!=null)return existing;
            // Bound work for modded high-power TNT. Beyond the budget the host
            // is resistant, while ordinary MC blocks retain vanilla behavior.
            if(cells.size()>=8192)return new Cell(Blocks.BEDROCK.getDefaultState(),false);
            Cell next=sample(pos);cells.put(pos.asLong(),next);return next;
        }
        private Cell sample(BlockPos pos) {
            if(!HostExplosionPacket.validCell(pos))return new Cell(Blocks.BEDROCK.getDefaultState(),false);
            var collision=host.collision();var middle=HostCollision.goldsrc(pos.getX()+.5,pos.getY()+.5,pos.getZ()+.5);
            boolean solid=collision.classify(null,HostCollision.goldsrc(new Box(pos)))!=BspMap.CLEAR;
            if(!solid) {
                int contents=collision.contents(null,0,middle);
                return new Cell(contents>=-5&&contents<=-3?Blocks.WATER.getDefaultState():Blocks.AIR.getDefaultState(),false);
            }
            // A newly exposed cut face retains the original world's material.
            // Sample outward from this remaining cell (not through a remote
            // front wall). Multiple nearby materials use the strongest one.
            int selected=MapMining.UNBREAKABLE;float resistance=-1;
            for(int axis=0;axis<3;axis++)for(int sign:new int[]{-1,1}) {
                var end=middle.add(new BspMap.Vec(axis==0?sign*96:0,axis==1?sign*96:0,axis==2?sign*96:0));
                String name=textures.texture(middle,end,budget);
                Integer material=name==null?null:catalog.get(name);
                if(material==null)continue;
                BlockState state=HostMiningMaterials.state(material);
                float value=state.getBlock().getBlastResistance();
                if(value>resistance){resistance=value;selected=material;}
            }
            return new Cell(HostMiningMaterials.state(selected),true);
        }
        public BlockState state(BlockPos pos) { return cell(pos).state(); }
        public void reached(BlockPos pos) { if(cell(pos).solid())reached.add(pos.toImmutable()); }
        public void commit(Explosion explosion) {
            if(committed)return;committed=true;
            if(!explosion.shouldDestroy()||host.epoch()!=epoch||host.mining().revision()!=policy
                ||host.mining().mode()!=MapMining.ALL_GEOMETRY||!GoldCraft.hostExplosionsAvailable())return;
            var finalCells=HostExplosionPacket.finalCells(center,power,reached,explosion.getAffectedBlocks());
            if(finalCells.isEmpty())return;
            if(event==Long.MAX_VALUE)throw new IllegalStateException("Host explosion event sequence exhausted");
            GoldCraft.sendHostExplosion(HostExplosionPacket.encode(epoch,policy,++event,center,power,finalCells));
        }
    }
}
