package dev.goldcraft.world;

import dev.goldcraft.bridge.MapMining;
import dev.goldcraft.bridge.Wire;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import java.util.*;

/** Bounded, canonical intersection of host ray hits and the final Mod event list. */
public final class HostExplosionPacket {
    public static final int MAX_CELLS=4096;
    private HostExplosionPacket() {}
    public static boolean validOrigin(Vec3d p,float power) {
        return Float.isFinite(power)&&power>0&&power<=16&&Double.isFinite(p.x)&&Double.isFinite(p.y)&&Double.isFinite(p.z)
            &&Math.abs(p.x)<=1024&&Math.abs(p.y-Wire.Y_OFFSET)<=1024&&Math.abs(p.z)<=1024;
    }
    public static boolean validCell(BlockPos p) {
        return p.getX()>=-1024&&p.getX()<1024&&p.getZ()>=-1024&&p.getZ()<1024
            &&p.getY()>=-960&&p.getY()<1088;
    }
    public static List<MapMining.Cell> finalCells(Vec3d center,float power,Set<BlockPos> reached,List<BlockPos> affected) {
        if(!validOrigin(center,power))return List.of();
        Set<MapMining.Cell> cells=new HashSet<>();double radius=power*2.0+2.0;
        for(BlockPos pos:reached) {
            if(!validCell(pos)||!affected.contains(pos))continue;
            double dx=pos.getX()+.5-center.x,dy=pos.getY()+.5-center.y,dz=pos.getZ()+.5-center.z;
            if(dx*dx+dy*dy+dz*dz>radius*radius)continue;
            cells.add(new MapMining.Cell(pos.getX(),-pos.getZ()-1,pos.getY()-64));
            if(cells.size()>MAX_CELLS)return List.of(); // fail closed as a whole explosion
        }
        return cells.stream().sorted(Comparator.comparingInt(MapMining.Cell::x).thenComparingInt(MapMining.Cell::y)
            .thenComparingInt(MapMining.Cell::z)).toList();
    }
    public static byte[] encode(long epoch,long policy,long event,Vec3d center,float power,List<MapMining.Cell> cells) {
        if(epoch==0||policy==0||event==0||!validOrigin(center,power)||cells.isEmpty()||cells.size()>MAX_CELLS)
            throw new IllegalArgumentException("Host explosion bounds");
        var w=new Wire.Writer().i64(epoch).i64(policy).i64(event).f32((float)center.x).f32((float)center.y)
            .f32((float)center.z).f32(power).i32(cells.size());
        for(var cell:cells)w.i32(cell.x()).i32(cell.y()).i32(cell.z());
        return w.toByteArray();
    }
}
