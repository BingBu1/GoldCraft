package dev.goldcraft.world;

import net.minecraft.util.hit.BlockHitResult;
import net.minecraft.util.hit.HitResult;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.Vec3d;
import net.minecraft.world.RaycastContext;
import net.minecraft.world.World;

public final class HostRaycast {
    private HostRaycast(){}
    public static BlockHitResult closest(World world,RaycastContext context,BlockHitResult vanilla) {
        var host=HostCollision.state(world);if(host==null||host.geometry()==null||!world.getRegistryKey().getValue().toString().equals(host.dimension()))return vanilla;
        Vec3d from=context.getStart(),to=context.getEnd();
        BspMap.Vec start=HostCollision.goldsrc(from.x,from.y,from.z),end=HostCollision.goldsrc(to.x,to.y,to.z);
        BspMap.Trace trace=host.geometry().trace(0,0,start,end);float fraction=trace.startSolid()?1:trace.fraction();BspMap.Vec normal=trace.normal();
        for(var brush:host.brushes()) {
            if(!brush.solid())continue;
            BspMap.Trace candidate=host.geometry().trace(brush.model(),0,HostCollision.local(start,brush),HostCollision.local(end,brush));
            if(!candidate.startSolid()&&candidate.fraction()<fraction){fraction=candidate.fraction();normal=HostCollision.worldDirection(candidate.normal(),brush);}
        }
        if(fraction>=1)return vanilla;
        Vec3d hit=from.lerp(to,fraction);
        if(vanilla.getType()!=HitResult.Type.MISS&&from.squaredDistanceTo(vanilla.getPos())<=from.squaredDistanceTo(hit))return vanilla;
        Vec3d direction=new Vec3d(normal.x(),normal.z(),-normal.y());
        Direction face=Direction.getFacing(direction.x,direction.y,direction.z);
        // BSP trace stops 1/32 GS unit before impact; choose the empty cell outside the host surface.
        BlockPos target=BlockPos.ofFloored(hit.add(direction.multiply(0.002)));
        return new BlockHitResult(hit,face,target,false);
    }
}
