package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.mixin.ChunkCacheAccessor;
import net.minecraft.util.function.BooleanBiFunction;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.util.shape.ArrayVoxelShape;
import net.minecraft.util.shape.BitSetVoxelSet;
import net.minecraft.util.shape.VoxelShape;
import net.minecraft.util.shape.VoxelShapes;
import net.minecraft.world.World;
import net.minecraft.world.BlockView;
import java.util.*;

/** Shared server/client host collision. Surface cells use 1/16 block (2 GoldSrc unit) resolution. */
public final class HostCollision {
    private static final Map<World,Cache> WORLDS=Collections.synchronizedMap(new WeakHashMap<>());
    private static final int RESOLUTION=16;
    private static final double[] POINTS=new double[RESOLUTION+1];
    static {for(int i=0;i<POINTS.length;i++)POINTS[i]=(double)i/RESOLUTION;}
    private HostCollision(){}
    public static void attach(World world,HostWorldState host){WORLDS.put(world,new Cache(host));}
    public static HostWorldState state(World world){Cache c=WORLDS.get(world);return c==null?null:c.host;}
    public static World world(BlockView view){
        if(view instanceof World world)return world;
        return view instanceof ChunkCacheAccessor cache?cache.goldcraft$world():null;
    }
    private static Cache active(BlockView view){
        World world=world(view);Cache cache=world==null?null:WORLDS.get(world);
        if(cache==null||!world.getRegistryKey().getValue().toString().equals(cache.host.dimension()))return null;
        cache.refresh();return cache.map==null?null:cache;
    }
    /** Local cell geometry, shared by physics and pathfinding without changing MC blocks. */
    public static VoxelShape shape(BlockView view,BlockPos pos){
        Cache cache=active(view);if(cache==null)return VoxelShapes.empty();
        VoxelShape fixed=cache.shape(pos,false),moving=cache.shape(pos,true);
        return fixed.isEmpty()?moving:moving.isEmpty()?fixed:VoxelShapes.union(fixed,moving);
    }
    public static long revision(BlockView view){Cache cache=active(view);return cache==null?0:cache.revision;}
    private static final class CellShape extends ArrayVoxelShape {CellShape(BitSetVoxelSet cells){super(cells,POINTS,POINTS,POINTS);}}
    private static final class Cache {
        final HostWorldState host;
        BspMap map;
        long revision;
        List<HostWorldState.Brush> snapshot=List.of();
        List<HostWorldState.Brush> brushState=List.of();
        final Map<Long,VoxelShape> staticShapes=lru(),movingShapes=lru();
        Cache(HostWorldState host){this.host=host;}
        private static Map<Long,VoxelShape> lru(){return new LinkedHashMap<>(256,0.75f,true){@Override protected boolean removeEldestEntry(Map.Entry<Long,VoxelShape> e){return size()>32768;}};}
        void refresh(){if(map!=host.geometry()){map=host.geometry();staticShapes.clear();movingShapes.clear();revision++;}if(snapshot!=host.brushes()){snapshot=host.brushes();var solids=snapshot.stream().filter(HostWorldState.Brush::solid).toList();if(!brushState.equals(solids)){brushState=solids;movingShapes.clear();revision++;}}}
        VoxelShape shape(BlockPos pos,boolean moving) {
            Map<Long,VoxelShape> shapes=moving?movingShapes:staticShapes;
            return shapes.computeIfAbsent(pos.asLong(),ignored->build(pos,moving));
        }
        VoxelShape build(BlockPos pos,boolean moving) {
            List<HostWorldState.Brush> nearby=moving?brushState.stream().filter(b->intersects(pos,b)).toList():List.of();
            if(moving&&nearby.isEmpty())return VoxelShapes.empty();
            BitSetVoxelSet cells=new BitSetVoxelSet(RESOLUTION,RESOLUTION,RESOLUTION);
            fill(cells,pos,0,0,0,RESOLUTION,moving,nearby);
            return cells.isEmpty()?VoxelShapes.empty():new CellShape(cells).simplify();
        }
        void fill(BitSetVoxelSet cells,BlockPos block,int x,int y,int z,int width,boolean moving,List<HostWorldState.Brush> nearby) {
            Box mc=new Box(block.getX()+(double)x/RESOLUTION,block.getY()+(double)y/RESOLUTION,block.getZ()+(double)z/RESOLUTION,
                block.getX()+(double)(x+width)/RESOLUTION,block.getY()+(double)(y+width)/RESOLUTION,block.getZ()+(double)(z+width)/RESOLUTION);
            BspMap.Bounds gs=goldsrc(mc);int classification;
            if(!moving)classification=map.classify(0,gs);
            else {
                classification=BspMap.CLEAR;
                for(var brush:nearby){int value=map.classify(brush.model(),local(gs,brush));if(value==BspMap.FILLED){classification=value;break;}if(value==BspMap.MIXED)classification=value;}
            }
            if(classification==BspMap.CLEAR)return;
            if(classification==BspMap.FILLED||width==1){for(int i=x;i<x+width;i++)for(int j=y;j<y+width;j++)for(int k=z;k<z+width;k++)cells.set(i,j,k);return;}
            int half=width/2;
            for(int i=0;i<2;i++)for(int j=0;j<2;j++)for(int k=0;k<2;k++)fill(cells,block,x+i*half,y+j*half,z+k*half,half,moving,nearby);
        }
    }
    private static boolean intersects(BlockPos pos,HostWorldState.Brush brush) {
        BspMap.Bounds b=goldsrc(new Box(pos));
        return b.max().x()>brush.min().x()&&b.min().x()<brush.max().x()&&b.max().y()>brush.min().y()&&b.min().y()<brush.max().y()&&b.max().z()>brush.min().z()&&b.min().z()<brush.max().z();
    }
    public static BspMap.Vec goldsrc(double x,double y,double z){return new BspMap.Vec((float)(x*Wire.UNITS_PER_BLOCK),(float)(-z*Wire.UNITS_PER_BLOCK),(float)((y-Wire.Y_OFFSET)*Wire.UNITS_PER_BLOCK));}
    public static BspMap.Bounds goldsrc(Box b){return new BspMap.Bounds(goldsrc(b.minX,b.minY,b.maxZ),goldsrc(b.maxX,b.maxY,b.minZ));}
    public static BspMap.Vec local(BspMap.Vec point,HostWorldState.Brush brush) {
        double pitch=Math.toRadians(brush.angles().x()),yaw=Math.toRadians(brush.angles().y()),roll=Math.toRadians(brush.angles().z());
        double sp=Math.sin(pitch),cp=Math.cos(pitch),sy=Math.sin(yaw),cy=Math.cos(yaw),sr=Math.sin(roll),cr=Math.cos(roll);
        double x=point.x()-brush.origin().x(),y=point.y()-brush.origin().y(),z=point.z()-brush.origin().z();
        // Engine AngleVectors: local x=forward, y=-right, z=up.
        return new BspMap.Vec((float)(x*cp*cy+y*cp*sy-z*sp),
            (float)(x*(sr*sp*cy-cr*sy)+y*(sr*sp*sy+cr*cy)+z*sr*cp),
            (float)(x*(cr*sp*cy+sr*sy)+y*(cr*sp*sy-sr*cy)+z*cr*cp));
    }
    public static BspMap.Vec worldDirection(BspMap.Vec normal,HostWorldState.Brush brush) {
        BspMap.Vec origin=new BspMap.Vec(brush.origin().x(),brush.origin().y(),brush.origin().z());
        BspMap.Vec x=local(origin.add(new BspMap.Vec(1,0,0)),brush),y=local(origin.add(new BspMap.Vec(0,1,0)),brush),z=local(origin.add(new BspMap.Vec(0,0,1)),brush);
        return new BspMap.Vec(x.dot(normal),y.dot(normal),z.dot(normal));
    }
    private static BspMap.Bounds local(BspMap.Bounds box,HostWorldState.Brush brush) {
        float minX=Float.POSITIVE_INFINITY,minY=minX,minZ=minX,maxX=Float.NEGATIVE_INFINITY,maxY=maxX,maxZ=maxX;
        for(int i=0;i<8;i++) {
            BspMap.Vec p=local(new BspMap.Vec((i&1)==0?box.min().x():box.max().x(),(i&2)==0?box.min().y():box.max().y(),(i&4)==0?box.min().z():box.max().z()),brush);
            minX=Math.min(minX,p.x());minY=Math.min(minY,p.y());minZ=Math.min(minZ,p.z());maxX=Math.max(maxX,p.x());maxY=Math.max(maxY,p.y());maxZ=Math.max(maxZ,p.z());
        }
        return new BspMap.Bounds(new BspMap.Vec(minX,minY,minZ),new BspMap.Vec(maxX,maxY,maxZ));
    }
    public static Iterable<VoxelShape> augment(World world,Box query,Iterable<VoxelShape> vanilla) {
        long began=Performance.begin();
        try {return augmentInternal(world,query,vanilla);}finally{Performance.end("hostCollision",began);}
    }
    private static Iterable<VoxelShape> augmentInternal(World world,Box query,Iterable<VoxelShape> vanilla) {
        Cache cache=WORLDS.get(world);if(cache==null)return vanilla;cache.refresh();
        if(cache.map==null||!world.getRegistryKey().getValue().toString().equals(cache.host.dimension()))return vanilla;
        int x0=(int)Math.floor(query.minX),y0=(int)Math.floor(query.minY),z0=(int)Math.floor(query.minZ);
        int x1=(int)Math.floor(query.maxX),y1=(int)Math.floor(query.maxY),z1=(int)Math.floor(query.maxZ);
        if((long)(x1-x0+1)*(y1-y0+1)*(z1-z0+1)>32768)throw new IllegalArgumentException("Host collision query exceeds cell budget");
        List<VoxelShape> result=new ArrayList<>();vanilla.forEach(result::add);VoxelShape queryShape=VoxelShapes.cuboid(query);
        for(int x=x0;x<=x1;x++)for(int y=y0;y<=y1;y++)for(int z=z0;z<=z1;z++) {
            BlockPos pos=new BlockPos(x,y,z);
            for(boolean moving:new boolean[]{false,true}) {
                VoxelShape shape=cache.shape(pos,moving);if(shape.isEmpty())continue;
                shape=shape.offset(x,y,z);if(VoxelShapes.matchesAnywhere(shape,queryShape,BooleanBiFunction.AND))result.add(shape);
            }
        }
        return result;
    }
}
