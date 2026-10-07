package dev.goldcraft.world;

import org.junit.jupiter.api.Test;
import java.util.List;
import static org.junit.jupiter.api.Assertions.*;

class HullSlideTest {
    private static BspMap.Vec v(float x,float y,float z){return new BspMap.Vec(x,y,z);}
    private static final BspMap.Vec ZERO=v(0,0,0);
    // Independent convex room oracle: x>=0, y>=0, z>=0. Contact has the same
    // 1/32 GS-unit separation as engine traces, without invoking the BSP walker.
    private static BspMap.Trace room(BspMap.Vec a,BspMap.Vec b) {
        float fraction=1;BspMap.Vec normal=ZERO;
        if(a.x()<0||a.y()<0||a.z()<0)return new BspMap.Trace(1,b,ZERO,0,true,true,false,false);
        for(int axis=0;axis<3;axis++)if(b.axis(axis)<0) {
            float f=Math.max(0,(a.axis(axis)-0.03125f)/(a.axis(axis)-b.axis(axis)));
            if(f<fraction){fraction=f;normal=axis==0?v(1,0,0):axis==1?v(0,1,0):v(0,0,1);}
        }
        return new BspMap.Trace(fraction,a.add(b.subtract(a).scale(fraction)),normal,0,false,false,true,false);
    }
    @Test void largeMotionStopsAtWallAndKeepsTangentialTravel() {
        var hit=HullSlide.move(v(20,20,36),v(-400,80,0),HullSlideTest::room);
        assertEquals(-19.96875,hit.delta().x(),0.0001);
        assertEquals(80,hit.delta().y(),0.0001);assertEquals(0,hit.delta().z());
        assertTrue(hit.blocked());assertFalse(hit.stuck());
    }
    @Test void cornerAndFloorConstrainAllThreeAxesWithoutReverseMotion() {
        var hit=HullSlide.move(v(20,30,40),v(-100,-200,-300),HullSlideTest::room);
        assertEquals(-19.96875,hit.delta().x(),0.0001);assertEquals(-29.96875,hit.delta().y(),0.0001);
        assertEquals(-39.96875,hit.delta().z(),0.0001);assertTrue(hit.grounded());
        var buried=HullSlide.move(v(-1,20,36),v(100,0,0),HullSlideTest::room);
        assertTrue(buried.stuck());assertEquals(ZERO,buried.delta());
    }
    @Test void freeMotionAndWallParallelMotionRemainUnchanged() {
        for(var movement:List.of(v(0,0,0),v(4,10,3),v(0,100,0))) {
            var result=HullSlide.move(v(0.03125f,20,36),movement,HullSlideTest::room);
            assertEquals(movement.x(),result.delta().x(),0.00001);assertEquals(movement.y(),result.delta().y(),0.00001);
            assertFalse(result.blocked());
        }
    }
}
