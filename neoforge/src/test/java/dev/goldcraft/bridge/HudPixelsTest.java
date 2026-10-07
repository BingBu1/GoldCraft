package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HudPixelsTest {
    @Test void emptyBackgroundHasBoundedIndependentRuns() {
        byte[] encoded=HudPixels.encode(ByteBuffer.wrap(new byte[512*129*4]),512,129);
        Wire.Reader r=new Wire.Reader(encoded);
        assertEquals(0xffff,r.u16());assertEquals(0,r.i32());
        assertEquals(0xffff,r.u16());assertEquals(0,r.i32());
        assertEquals(0x81ff,r.u16());assertEquals(0,r.i32());r.finish();
    }
    @Test void onePixelPreservesChannelsAndBufferPosition() {
        var data=ByteBuffer.wrap(new byte[]{99,1,2,3,(byte)128});data.position(1);
        assertArrayEquals(new byte[]{0,0,1,2,3,(byte)128},HudPixels.encode(data,1,1));assertEquals(1,data.position());
    }
    @Test void rejectsInvalidSizesBeforeAllocationOrIteration() {
        assertThrows(IllegalArgumentException.class,()->HudPixels.encode(ByteBuffer.allocate(0),Integer.MAX_VALUE,1024));
        assertThrows(IllegalArgumentException.class,()->HudPixels.encode(ByteBuffer.allocate(3),1,1));
        assertThrows(IllegalArgumentException.class,()->HudPixels.encode(ByteBuffer.allocate(0),0,1));
    }
}
