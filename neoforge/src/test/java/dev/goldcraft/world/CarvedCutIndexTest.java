package dev.goldcraft.world;

import dev.goldcraft.bridge.MapEdits;
import java.util.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class CarvedCutIndexTest {
    private static BspMap.Vec v(float x,float y,float z){return new BspMap.Vec(x,y,z);}
    @Test void exactOrderedCandidatesForRandomOverlappingTouchingAndDuplicateCuts() {
        var random=new Random(621991);var cuts=new ArrayList<MapEdits.Box>();
        for(int i=0;i<4096;i++) {
            float x=random.nextInt(128)*32,y=random.nextInt(128)*32,z=random.nextInt(32)*32;
            cuts.add(new MapEdits.Box(x,y,z,x+32,y+32,z+32));
        }
        cuts.add(cuts.getFirst());var index=new CarvedCutIndex(cuts);
        for(int i=0;i<5000;i++) {
            var b=cuts.get(i%cuts.size());
            float x=i%2==0?b.maxX():random.nextFloat()*4096,y=b.minY(),z=b.minZ();
            var q=new BspMap.Bounds(v(x,y,z),v(x+32,y+32,z+32));
            var reference=cuts.stream().filter(c->c.maxX()>q.min().x()&&c.minX()<q.max().x()
                &&c.maxY()>q.min().y()&&c.minY()<q.max().y()&&c.maxZ()>q.min().z()&&c.minZ()<q.max().z()).toList();
            assertEquals(reference,index.query(q));
        }
        assertTrue(index.query(new BspMap.Bounds(v(-64,-64,-64),v(0,0,0))).isEmpty());
        assertTrue(new CarvedCutIndex(List.of()).query(new BspMap.Bounds(v(0,0,0),v(32,32,32))).isEmpty());
    }
}
