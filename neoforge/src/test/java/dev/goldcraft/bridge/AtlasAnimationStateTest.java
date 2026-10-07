package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class AtlasAnimationStateTest {
    @Test void coalescedSnapshotKeepsLatestFastAndSlowSprites() {
        AtlasAnimationState state=new AtlasAnimationState();state.reset(32,16);
        state.update(0,0,1,1,new byte[]{1,2,3,4});
        state.update(16,0,1,1,new byte[]{5,6,7,8});state.sent();
        byte[] fast={9,10,11,12};state.update(0,0,1,1,fast);fast[0]=0;
        assertTrue(state.dirty());
        Wire.Reader r=new Wire.Reader(state.snapshot(42,7));
        assertEquals(42,r.i64());assertEquals(7,r.i64());assertEquals(2,r.i32());
        assertEquals(0,r.i32());assertEquals(0,r.i32());assertEquals(1,r.i32());assertEquals(1,r.i32());
        assertArrayEquals(new byte[]{9,10,11,12},r.bytes(4));
        assertEquals(16,r.i32());assertEquals(0,r.i32());assertEquals(1,r.i32());assertEquals(1,r.i32());
        assertArrayEquals(new byte[]{5,6,7,8},r.bytes(4));r.finish();
        state.sent();state.update(0,0,1,1,new byte[]{9,10,11,12});assertFalse(state.dirty());
        state.reset(16,16);assertFalse(state.dirty());
        assertEquals(0,new Wire.Reader(java.util.Arrays.copyOfRange(state.snapshot(42,8),16,20)).i32());
    }
    @Test void invalidRectanglesAndOverflowAreRejected() {
        AtlasAnimationState state=new AtlasAnimationState();state.reset(16,16);
        assertThrows(IllegalArgumentException.class,()->state.update(16,0,1,1,new byte[4]));
        assertThrows(IllegalArgumentException.class,()->state.update(-1,0,1,1,new byte[4]));
        assertThrows(IllegalArgumentException.class,()->state.update(0,0,Integer.MAX_VALUE,Integer.MAX_VALUE,new byte[4]));
        assertThrows(IllegalArgumentException.class,()->state.update(0,0,1,1,new byte[3]));
    }
}
