package dev.goldcraft.world;

import java.util.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
import static dev.goldcraft.world.CarvedVolume.*;

class CarvedCutUnionTest {
    private static boolean contains(List<Box> boxes,P p) {
        return boxes.stream().anyMatch(b->p.x()>=b.min().x()&&p.x()<=b.max().x()
            &&p.y()>=b.min().y()&&p.y()<=b.max().y()&&p.z()>=b.min().z()&&p.z()<=b.max().z());
    }
    @Test void denseCubeCollapsesWithoutFillingMissingVoxels() {
        var boxes=new ArrayList<Box>();
        for(int x=0;x<11;x++)for(int y=0;y<11;y++)for(int z=0;z<11;z++)
            boxes.add(new Box(new P(x*32,y*32,z*32),new P((x+1)*32,(y+1)*32,(z+1)*32)));
        assertEquals(1,CarvedCutUnion.compact(boxes).size());
        boxes.removeIf(b->b.min().equals(new P(160,160,160)));
        var compact=CarvedCutUnion.compact(boxes);
        assertFalse(contains(compact,new P(176,176,176)));
        for(double x=-.5;x<=11.5;x+=.5)for(double y=-.5;y<=11.5;y+=.5)for(double z=-.5;z<=11.5;z+=.5) {
            var p=new P(x*32,y*32,z*32);assertEquals(contains(boxes,p),contains(compact,p));
        }
    }
    @Test void overlapsDuplicatesAndFractionalGapsKeepExactUnion() {
        var random=new Random(4251);var boxes=new ArrayList<Box>();
        for(int i=0;i<200;i++) {
            var p=new P(random.nextInt(16),random.nextInt(16),random.nextInt(16));
            boxes.add(new Box(p,p.add(new P(1,1,1))));
        }
        boxes.addAll(List.copyOf(boxes));
        boxes.add(new Box(new P(100,0,0),new P(101,1,1)));
        boxes.add(new Box(new P(101.00001,0,0),new P(102,1,1)));
        var compact=CarvedCutUnion.compact(boxes);
        assertFalse(contains(compact,new P(101.000005,.5,.5)));
        for(int i=0;i<10000;i++) {
            var p=new P(random.nextDouble()*17,random.nextDouble()*17,random.nextDouble()*17);
            assertEquals(contains(boxes,p),contains(compact,p));
        }
    }
}
