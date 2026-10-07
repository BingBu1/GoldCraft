import dev.goldcraft.world.BspMap;
import java.nio.file.*;
import java.util.*;

/** Print bounded local hull observations when choosing real-map movement fixtures. */
public final class HostMapQuery {
    static BspMap.Vec v(float x,float y,float z){return new BspMap.Vec(x,y,z);}
    public static void main(String[] args)throws Exception {
        var map=BspMap.read(Files.readAllBytes(Path.of(args[0])));
        float x=Float.parseFloat(args[1]),y=Float.parseFloat(args[2]),z=Float.parseFloat(args[3]);
        float above=args.length>4?Float.parseFloat(args[4]):200;
        for(float dy:new float[]{0,-1,-2,-4,-8}) {
            var box=new BspMap.Bounds(v(x-9.6f,y+dy-9.6f,z),v(x+9.6f,y+dy+9.6f,z+57.6f));
            System.out.println("player bbox dy="+dy+" h0="+map.classify(0,box));
        }
        for(float yy=y-96;yy<=y+96;yy+=16) {
            System.out.printf(Locale.ROOT,"y=%7.1f",yy);
            for(float xx=x-64;xx<=x+128;xx+=16) {
                var hit=map.trace(0,1,v(xx,yy,z+above),v(xx,yy,z-180));
                System.out.printf(Locale.ROOT," %7.1f",hit.startSolid()?-999f:hit.fraction()>=1?-888f:hit.end().z()-36);
            }
            System.out.println();
        }
        for(int hull:new int[]{0,1,3})for(var dir:List.of(v(128,0,0),v(-128,0,0),v(0,128,0),v(0,-128,0))) {
            var from=v(x,y,z+(hull==1?36:hull==3?18:0));var hit=map.trace(0,hull,from,from.add(dir));
            System.out.println("hull="+hull+" from="+from+" dir="+dir+" hit="+hit);
        }
    }
}
