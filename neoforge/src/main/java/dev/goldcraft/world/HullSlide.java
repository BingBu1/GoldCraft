package dev.goldcraft.world;

import java.util.ArrayList;
import java.util.List;
import dev.goldcraft.world.BspMap.Vec;

/** Sweeps an already expanded GoldSrc hull. No entity radius is added here. */
public final class HullSlide {
    @FunctionalInterface public interface Sweep { BspMap.Trace trace(Vec start,Vec end); }
    public record Result(Vec delta,boolean blocked,boolean grounded,boolean stuck) {}
    private static final Vec ZERO=new Vec(0,0,0);
    private HullSlide(){}

    public static Result move(Vec start,Vec delta,Sweep sweep) {
        Vec point=start,remaining=delta;
        List<Vec> planes=new ArrayList<>(5);
        boolean blocked=false,grounded=false;
        for(int bump=0;bump<4&&remaining.dot(remaining)>1e-10f;bump++) {
            var hit=sweep.trace(point,point.add(remaining));
            if(hit.startSolid()||hit.allSolid())return new Result(point.subtract(start),true,grounded,true);
            if(hit.fraction()>0)point=point.add(remaining.scale(hit.fraction()));
            if(hit.fraction()>=1)break;
            blocked=true;grounded|=hit.normal().z()>0.7f;
            remaining=remaining.scale(1-hit.fraction());
            boolean repeated=planes.stream().anyMatch(p->p.dot(hit.normal())>0.999f);
            if(!repeated)planes.add(hit.normal());
            Vec before=remaining;boolean clipped=false;
            for(Vec plane:planes) {
                Vec candidate=clip(before,plane);
                if(planes.stream().allMatch(p->candidate.dot(p)>=-0.0001f)) {remaining=candidate;clipped=true;break;}
            }
            if(!clipped) {
                if(planes.size()!=2)break;
                Vec a=planes.get(0),b=planes.get(1);
                Vec crease=new Vec(a.y()*b.z()-a.z()*b.y(),a.z()*b.x()-a.x()*b.z(),a.x()*b.y()-a.y()*b.x());
                float length2=crease.dot(crease);
                remaining=length2>1e-8f?crease.scale(crease.dot(before)/length2):ZERO;
                boolean outside=false;for(Vec plane:planes)outside|=remaining.dot(plane)<-0.0001f;
                if(outside)break;
            }
            if(remaining.dot(delta)<=0)break;
        }
        return new Result(point.subtract(start),blocked,grounded,false);
    }
    private static Vec clip(Vec movement,Vec normal) {
        float into=movement.dot(normal);
        return into<0?movement.subtract(normal.scale(into)):movement;
    }
}
