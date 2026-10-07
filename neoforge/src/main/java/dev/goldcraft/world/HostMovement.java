package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Performance;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityPose;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.util.math.Box;
import net.minecraft.util.math.Vec3d;

/** The BSP render hull alone omits player-clip brushes. Apply the player clip hull as well
 * as Minecraft's block/entity collision, on both prediction and authoritative movement. */
public final class HostMovement {
    private HostMovement(){}
    public static long constrainedMoves,recoveredMoves,stuckMoves;
    private static final float SCALE=Wire.UNITS_PER_BLOCK;
    private static final double EPSILON=0.000001;

    private static HostWorldState state(Entity entity) {
        if(entity.noClip||entity.isSpectator())return null;
        var host=HostCollision.state(entity.getWorld());
        return host!=null&&host.geometry()!=null&&entity.getWorld().getRegistryKey().getValue().toString().equals(host.dimension())?host:null;
    }
    public static int hull(Entity entity) {return hull(entity.getPose());}
    private static int hull(EntityPose pose) {
        return pose==EntityPose.CROUCHING||pose==EntityPose.SWIMMING||pose==EntityPose.FALL_FLYING?3:1;
    }
    private static BspMap.Vec center(Vec3d feet,int hull) {
        // Standard GoldSrc hulls are already Minkowski-expanded: standing +/-16,36;
        // crouching +/-16,18 GS units. Minecraft keeps its own smaller body and pose
        // checks as an additional constraint. Do not expand these clip hulls again.
        return HostCollision.goldsrc(feet.x,feet.y+(hull==3?18.0:36.0)/SCALE,feet.z);
    }
    private static BspMap.Vec direction(Vec3d v){return new BspMap.Vec((float)(v.x*SCALE),(float)(-v.z*SCALE),(float)(v.y*SCALE));}
    private static Vec3d minecraft(BspMap.Vec v){return new Vec3d(v.x()/SCALE,v.z()/SCALE,-v.y()/SCALE);}

    private static boolean nearBrush(BspMap.Vec a,BspMap.Vec b,int hull,HostWorldState.Brush brush) {
        float h=hull==3?18:36;
        return Math.max(a.x(),b.x())+17>=brush.min().x()&&Math.min(a.x(),b.x())-17<=brush.max().x()
            &&Math.max(a.y(),b.y())+17>=brush.min().y()&&Math.min(a.y(),b.y())-17<=brush.max().y()
            &&Math.max(a.z(),b.z())+h+1>=brush.min().z()&&Math.min(a.z(),b.z())-h-1<=brush.max().z();
    }
    private static BspMap.Trace trace(HostWorldState host,int hull,BspMap.Vec from,BspMap.Vec to) {
        var best=host.geometry().trace(0,hull,from,to);
        if(best.startSolid()||best.allSolid())return best;
        for(var brush:host.brushes()) {
            if(!brush.solid()||!nearBrush(from,to,hull,brush))continue;
            var hit=host.geometry().trace(brush.model(),hull,HostCollision.local(from,brush),HostCollision.local(to,brush));
            if(hit.startSolid()||hit.allSolid()||hit.fraction()<best.fraction()) {
                best=new BspMap.Trace(hit.fraction(),from.add(to.subtract(from).scale(hit.fraction())),
                    HostCollision.worldDirection(hit.normal(),brush),hit.planeDistance(),hit.startSolid(),hit.allSolid(),hit.inOpen(),hit.inWater());
                if(best.startSolid()||best.allSolid())return best;
            }
        }
        return best;
    }
    private static boolean fits(HostWorldState host,int hull,Vec3d feet) {
        var point=center(feet,hull);
        if(host.geometry().contents(0,hull,point)==BspMap.SOLID)return false;
        for(var brush:host.brushes())if(brush.solid()&&nearBrush(point,point,hull,brush)
            &&host.geometry().contents(brush.model(),hull,HostCollision.local(point,brush))==BspMap.SOLID)return false;
        return true;
    }
    private static Vec3d exactUnchanged(Vec3d result,Vec3d requested) {
        return new Vec3d(Math.abs(result.x-requested.x)<EPSILON?requested.x:result.x,
            Math.abs(result.y-requested.y)<EPSILON?requested.y:result.y,
            Math.abs(result.z-requested.z)<EPSILON?requested.z:result.z);
    }
    private static Vec3d voxel(Entity entity,Vec3d offset,Vec3d motion) {
        Box box=entity.getBoundingBox().offset(offset);
        return Entity.adjustMovementForCollisions(entity,motion,box,entity.getWorld(),entity.getWorld().getEntityCollisions(entity,box.stretch(motion)));
    }
    private static Vec3d constrain(Entity entity,HostWorldState host,int hull,Vec3d offset,Vec3d motion) {
        var from=center(entity.getPos().add(offset),hull);
        var clipped=HullSlide.move(from,direction(motion),(a,b)->trace(host,hull,a,b));
        Vec3d result=clipped.blocked()?exactUnchanged(minecraft(clipped.delta()),motion):motion;
        // Sliding against a slanted host plane can alter the requested direction.
        // Recheck Minecraft blocks/entities before accepting that new path.
        Vec3d safe=voxel(entity,offset,result);
        if(safe.squaredDistanceTo(result)>1e-12) {
            var recheck=trace(host,hull,from,from.add(direction(safe)));
            result=recheck.startSolid()||recheck.allSolid()?Vec3d.ZERO:safe.multiply(recheck.fraction());
        }
        return exactUnchanged(result,motion);
    }
    private static Vec3d segment(Entity entity,HostWorldState host,int hull,Vec3d offset,Vec3d motion) {
        return constrain(entity,host,hull,offset,voxel(entity,offset,motion));
    }
    public static Vec3d adjust(Entity entity,Vec3d requested,Vec3d vanilla) {
        if(!(entity instanceof PlayerEntity)||requested.lengthSquared()==0)return vanilla;
        var host=state(entity);if(host==null)return vanilla;
        long began=Performance.begin();
        try {
            int hull=hull(entity);
            if(!fits(host,hull,entity.getPos())) {
                // Bounded local unsticking after activation/brush motion. Never accept
                // a start-solid trace as permission to traverse an entire solid volume.
                for(double distance:new double[]{1.0/1024,1.0/32,1.0/16,1.0/8,1.0/4,1.0/2}) {
                    for(Vec3d offset:new Vec3d[]{new Vec3d(0,distance,0),new Vec3d(distance,0,0),new Vec3d(-distance,0,0),new Vec3d(0,0,distance),new Vec3d(0,0,-distance)}) {
                        if(fits(host,hull,entity.getPos().add(offset))&&entity.getWorld().isSpaceEmpty(entity,entity.getBoundingBox().offset(offset).contract(EPSILON))) {
                            recoveredMoves++;return offset;
                        }
                    }
                }
                stuckMoves++;return Vec3d.ZERO;
            }
            Vec3d flat=constrain(entity,host,hull,Vec3d.ZERO,vanilla);
            // At a clip-hull ledge the larger GoldSrc footprint can already be
            // supported while Minecraft's smaller footprint is not. Its Y-first
            // solver then falls and clips horizontal motion against a step face.
            // Also solve the requested motion with the hull support applied first.
            Vec3d direct=constrain(entity,host,hull,Vec3d.ZERO,requested);
            if(direct.horizontalLengthSquared()>flat.horizontalLengthSquared()+1e-10)flat=direct;
            boolean blockedHorizontal=Math.abs(flat.x-requested.x)>EPSILON||Math.abs(flat.z-requested.z)>EPSILON;
            if(blockedHorizontal&&entity.getStepHeight()>0&&(entity.isOnGround()||requested.y<0&&supported(entity))) {
                double step=Math.min(entity.getStepHeight(),18.0/SCALE);
                Vec3d up=segment(entity,host,hull,Vec3d.ZERO,new Vec3d(0,step,0));
                if(up.y>EPSILON) {
                    Vec3d across=segment(entity,host,hull,up,new Vec3d(requested.x,0,requested.z));
                    Vec3d down=segment(entity,host,hull,up.add(across),new Vec3d(0,Math.min(0,vanilla.y)-up.y,0));
                    Vec3d stepped=up.add(across).add(down);
                    if(stepped.horizontalLengthSquared()>flat.horizontalLengthSquared()+1e-10)flat=stepped;
                }
            }
            if(flat.squaredDistanceTo(vanilla)>1e-12)constrainedMoves++;
            return exactUnchanged(flat,vanilla);
        } finally {Performance.end("hostHullMovement",began);}
    }
    public static boolean canPose(PlayerEntity player,EntityPose pose) {
        var host=state(player);return host==null||fits(host,hull(pose),player.getPos());
    }
    public static boolean supported(Entity entity) {
        if(!(entity instanceof PlayerEntity))return false;
        var host=state(entity);if(host==null)return false;
        var from=center(entity.getPos(),hull(entity));
        var hit=trace(host,hull(entity),from,from.add(new BspMap.Vec(0,0,-17.6f)));
        return !hit.startSolid()&&!hit.allSolid()&&hit.fraction()<1&&hit.normal().z()>0.7;
    }
    public static HostWorldState.Brush ladder(LivingEntity entity) {
        var host=state(entity);if(host==null)return null;
        int hull=hull(entity);var point=center(entity.getPos(),hull);
        for(var brush:host.brushes())if(brush.ladder()&&nearBrush(point,point,hull,brush)) {
            if(entity instanceof PlayerEntity) {
                if(host.geometry().contents(brush.model(),hull,HostCollision.local(point,brush))!=BspMap.EMPTY)return brush;
            } else {
                // Ordinary mobs retain their actual bounding box. Only players use
                // GoldSrc's fixed standing/duck hulls.
                var box=entity.getBoundingBox();
                var min=HostCollision.local(HostCollision.goldsrc(box.minX,box.minY,box.maxZ),brush);
                var max=HostCollision.local(HostCollision.goldsrc(box.maxX,box.maxY,box.minZ),brush);
                if(brush.angles().x()==0&&brush.angles().y()==0&&brush.angles().z()==0
                    &&host.geometry().classify(brush.model(),new BspMap.Bounds(min,max))!=BspMap.CLEAR)return brush;
            }
        }
        return null;
    }
}
