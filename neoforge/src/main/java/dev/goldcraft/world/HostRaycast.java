package dev.goldcraft.world;

import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.hit.HitResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.RaycastContext;
import net.minecraft.world.World;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.MapMining;

public final class HostRaycast {
    private HostRaycast(){}
    public static final class Hit extends BlockHitResult {
        public final int slot,serial,model;
        public final MapMining.Cell miningRayCell;
        public final BlockPos miningWorldCell;
        private Hit(Vec3d point,Direction face,BlockPos position,int slot,int serial,int model,
                    MapMining.Cell miningRayCell,BlockPos miningWorldCell){
            super(point,face,position,false);this.slot=slot;this.serial=serial;this.model=model;
            this.miningRayCell=miningRayCell;this.miningWorldCell=miningWorldCell;
        }
    }
    public static BlockHitResult closest(World world,RaycastContext context,BlockHitResult vanilla) {
        var host=HostCollision.state(world);if(host==null||host.geometry()==null||!world.getRegistryKey().getValue().toString().equals(host.dimension()))return vanilla;
        Vec3d from=context.getStart(),to=context.getEnd();
        var hit=trace(host,from,to);
        if(hit==null||vanilla.getType()!=HitResult.Type.MISS&&from.squaredDistanceTo(vanilla.getPos())<=from.squaredDistanceTo(hit.getPos()))return vanilla;
        return hit;
    }
    public static Hit trace(HostWorldState host,Vec3d from,Vec3d to){
        if(host.geometry()==null)return null;
        BspMap.Vec start=HostCollision.goldsrc(from.x,from.y,from.z),end=HostCollision.goldsrc(to.x,to.y,to.z);
        BspMap.Trace trace=host.collision().trace(null,0,start,end);float fraction=trace.startSolid()?1:trace.fraction();BspMap.Vec normal=trace.normal();
        BspMap.Trace selected=trace;BspMap.Vec selectedStart=start,selectedEnd=end;
        HostWorldState.Brush selectedBrush=null;
        int slot=0,serial=0,model=0;
        for(var brush:host.brushes()) {
            if(!brush.solid())continue;
            var localStart=HostCollision.local(start,brush);var localEnd=HostCollision.local(end,brush);
            BspMap.Trace candidate=host.collision().trace(brush,0,localStart,localEnd);
            if(!candidate.startSolid()&&candidate.fraction()<fraction){
                fraction=candidate.fraction();normal=HostCollision.worldDirection(candidate.normal(),brush);
                slot=brush.slot();serial=brush.serial();model=brush.model();
                selected=candidate;selectedStart=localStart;selectedEnd=localEnd;
                selectedBrush=brush;
            }
        }
        if(fraction>=1)return null;
        Vec3d hit=from.lerp(to,fraction);
        Vec3d direction=new Vec3d(normal.x(),normal.z(),-normal.y());
        Direction face=Direction.getFacing(direction.x,direction.y,direction.z);
        // BSP trace stops 1/32 GS unit before impact; choose the empty cell outside the host surface.
        BlockPos target=BlockPos.ofFloored(hit.add(direction.multiply(0.002)));
        MapMining.Cell miningRayCell=null;
        BlockPos miningWorldCell=null;
        try {
            if(slot!=0)miningRayCell=HostMiningCoordinates.rayCell(selectedStart,selectedEnd,selected);
            miningWorldCell=HostMiningCoordinates.worldCell(start,end,selected,selectedBrush);
        } catch(IllegalArgumentException ignored) { /* Keep combat rays; this hit cannot start mining. */ }
        return new Hit(hit,face,target,slot,serial,model,miningRayCell,miningWorldCell);
    }
}
