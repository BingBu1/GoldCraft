import dev.goldcraft.world.BspMap;
import java.nio.file.*;
import java.util.*;

/** Locate short real BSP steps suitable for a movement regression route. */
public final class HostStepProbe {
    static BspMap.Vec v(float x,float y,float z){return new BspMap.Vec(x,y,z);}
    public static void main(String[] args)throws Exception {
        var map=BspMap.read(Files.readAllBytes(Path.of(args[0])));var found=new ArrayList<BspMap.Vec>();
        for(float top:new float[]{144,448,640})for(float x=-1400;x<896;x+=16)for(float y=32;y<3000;y+=16) {
            if(found.size()>=10)return;
            var floor=map.trace(0,1,v(x,y,top),v(x,y,top-192));
            if(floor.startSolid()||floor.fraction()>=1||floor.normal().z()<.99f)continue;
            var start=floor.end();
            for(var dir:List.of(v(1,0,0),v(-1,0,0),v(0,1,0),v(0,-1,0))) {
                var end=start.add(dir.scale(48));
                var across=map.trace(0,1,start,end);if(across.fraction()>=1||across.startSolid()||Math.abs(across.normal().z())>.1)continue;
                var landing=map.trace(0,1,end.add(v(0,0,18)),end.add(v(0,0,-2)));
                float rise=landing.end().z()-start.z();
                if(landing.startSolid()||landing.fraction()>=1||landing.normal().z()<.99f||rise<1||rise>18)continue;
                var lift=map.trace(0,1,start,start.add(v(0,0,18)));
                if(lift.fraction()<1||map.trace(0,1,lift.end(),end.add(v(0,0,18))).fraction()<1)continue;
                boolean close=false;for(var prev:found)if(Math.abs(prev.x()-x)+Math.abs(prev.y()-y)<160)close=true;
                if(close)continue;found.add(start);
                System.out.printf(Locale.ROOT,"{\"startGS\":[%.5f,%.5f,%.5f],\"landingGS\":[%.5f,%.5f,%.5f],\"riseGS\":%.5f}%n",start.x(),start.y(),start.z(),landing.end().x(),landing.end().y(),landing.end().z(),rise);
            }
        }
    }
}
