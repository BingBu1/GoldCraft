package dev.goldcraft.world;

import java.util.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
import static dev.goldcraft.world.CarvedVolume.*;

class CarvedBoundsIndexTest {
    @Test void exactOrderedCandidatesMatchLinearQueries() {
        var random=new Random(96312);var boxes=new ArrayList<Box>();
        for(int i=0;i<300;i++) {
            var p=new P(random.nextDouble()*200-100,random.nextDouble()*200-100,random.nextDouble()*200-100);
            boxes.add(new Box(p,p.add(new P(4,6,8))));
        }
        boxes.add(new Box(new P(0,0,0),new P(Math.scalb(1.,-30),1,1)));
        boxes.add(boxes.getFirst());
        var index=new CarvedBoundsIndex(boxes,new Work(1_000_000,4096));
        var found=new CarvedBoundsIndex.Candidates();
        for(int i=0;i<5000;i++) {
            P a,b;
            if(i<boxes.size()) {a=boxes.get(i).max();b=a;}
            else {a=new P(random.nextDouble()*200-100,random.nextDouble()*200-100,random.nextDouble()*200-100);b=a.add(new P(i%5,0,0));}
            index.query(a,b,found,new Work(1_000_000,4096));int at=0;
            for(int j=0;j<boxes.size();j++)if(boxes.get(j).lineBounds(a,b))assertEquals(j,found.get(at++));
            assertEquals(at,found.size());
        }
        assertThrows(IllegalArgumentException.class,()->new CarvedBoundsIndex(boxes,new Work(20,4096)));
        assertThrows(IllegalArgumentException.class,()->index.query(new P(-200,-200,-200),new P(200,200,200),found,new Work(1,4096)));
        new CarvedBoundsIndex(List.of(),new Work(1,1)).query(ZERO,ZERO,found,null);assertEquals(0,found.size());
    }
    @Test void indexedSpansKeepBoundaryPlaneTieOrder() throws Exception {
        var cells=new ArrayList<CollisionCell>();
        for(int i=0;i<128;i++) {
            var box=new Box(new P(i*4,-4,-4),new P(i*4+2,4,4));
            cells.addAll(expand(carve(new BspMap.Hull(new BspMap.Plane[0],new BspMap.Node[0],-2),box,List.of(),new Work(1_000_000,4096)),ZERO_BOX(),new Work(1_000_000,4096)));
        }
        var index=new CarvedBoundsIndex(cells.stream().map(CollisionCell::bounds).toList(),new Work(1_000_000,4096));
        var threads=new ArrayList<Thread>();var failure=new java.util.concurrent.atomic.AtomicReference<Throwable>();
        for(int k=0;k<4;k++) {
            var t=new Thread(()->{try {
                for(int i=0;i<256;i++) {
                    var a=new P(i-2,0,0);var b=new P(i+5,0,0);
                    assertEquals(contains(cells,a),contains(cells,index,a));
                    assertEquals(spans(cells,a,b),spans(cells,index,a,b));
                    assertEquals(spans(cells,b,a),spans(cells,index,b,a));
                }
            }catch(Throwable e){failure.compareAndSet(null,e);}});
            threads.add(t);t.start();
        }
        for(var t:threads)t.join();if(failure.get()!=null)throw new AssertionError(failure.get());
    }
    private static Box ZERO_BOX(){return new Box(ZERO,ZERO);}
}
