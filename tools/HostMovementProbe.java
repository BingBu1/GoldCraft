import dev.goldcraft.world.BspMap;
import java.nio.file.*;
import java.util.*;

/** Find reproducible player-clip routes in an actual sandbox BSP, not synthetic boxes. */
public final class HostMovementProbe {
    private static BspMap.Vec v(float x,float y,float z){return new BspMap.Vec(x,y,z);}
    public static void main(String[] args)throws Exception {
        var map=BspMap.read(Files.readAllBytes(Path.of(args[0])));
        var bounds=map.bounds(0);var found=new ArrayList<float[]>();
        for(float z=36.03125f;z<780&&found.size()<12;z+=32) {
            for(float x=bounds.min().x()+32;x<bounds.max().x()&&found.size()<12;x+=32) {
                for(float y=bounds.min().y()+32;y<bounds.max().y()&&found.size()<12;y+=32) {
                    var inside=v(x,y,z);
                    if(map.contents(0,1,inside)!=BspMap.SOLID)continue;
                    if(map.classify(0,new BspMap.Bounds(v(x-15.9f,y-15.9f,z-35.9f),v(x+15.9f,y+15.9f,z+35.9f)))!=BspMap.CLEAR)continue;
                    for(var direction:List.of(v(1,0,0),v(-1,0,0),v(0,1,0),v(0,-1,0))) {
                        var start=inside.subtract(direction.scale(96));
                        if(map.contents(0,1,start)==BspMap.SOLID)continue;
                        var floor=map.trace(0,1,start,start.add(v(0,0,-72)));
                        if(floor.startSolid()||floor.fraction()>=1||floor.normal().z()<0.7f)continue;
                        start=floor.end();var end=start.add(direction.scale(160));
                        var hit=map.trace(0,1,start,end);
                        if(hit.startSolid()||hit.fraction()>=1||Math.abs(hit.normal().z())>0.1)continue;
                        var min=v(Math.min(start.x(),end.x())-15.9f,Math.min(start.y(),end.y())-15.9f,start.z()-35.9f);
                        var max=v(Math.max(start.x(),end.x())+15.9f,Math.max(start.y(),end.y())+15.9f,start.z()+35.9f);
                        if(map.classify(0,new BspMap.Bounds(min,max))!=BspMap.CLEAR)continue;
                        boolean close=false;for(var a:found)if(Math.abs(a[0]-start.x())+Math.abs(a[1]-start.y())<160)close=true;
                        if(close)continue;
                        found.add(new float[]{start.x(),start.y(),start.z()});
                        System.out.printf(Locale.ROOT,"{\"startGS\":[%.6f,%.6f,%.6f],\"endGS\":[%.6f,%.6f,%.6f],\"stopGS\":[%.6f,%.6f,%.6f],\"normal\":[%.1f,%.1f,%.1f],\"fraction\":%.9f,\"renderHullClear\":true}%n",start.x(),start.y(),start.z(),end.x(),end.y(),end.z(),hit.end().x(),hit.end().y(),hit.end().z(),hit.normal().x(),hit.normal().y(),hit.normal().z(),hit.fraction());
                    }
                }
            }
        }
        if(found.isEmpty())throw new IllegalStateException("No grounded invisible-wall route found");
    }
}
