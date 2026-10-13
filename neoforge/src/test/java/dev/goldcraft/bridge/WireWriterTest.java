package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.function.Consumer;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class WireWriterTest {
    @Test void boundedWriterMatchesIndependentPrimitiveLayoutAndLeavesGuardsUntouched() {
        byte[] text="矿a".getBytes(StandardCharsets.UTF_8),source={91,5,6,7,92};
        var expected=ByteBuffer.allocate(1+2+4+8+4+2+text.length+3).order(ByteOrder.LITTLE_ENDIAN);
        expected.put((byte)0xab).putShort((short)0xcdef).putInt(0x87654321).putLong(0x1122334455667788L)
            .putFloat(-0.0f).putShort((short)text.length).put(text).put(source,1,3);
        byte[] destination=new byte[expected.capacity()+9];Arrays.fill(destination,(byte)0x5a);
        try(var writer=Wire.Writer.bounded(destination,4,expected.capacity())){
            writer.u8(0xab).u16(0xcdef).i32(0x87654321).i64(0x1122334455667788L).f32(-0.0f)
                .string("矿a").bytes(source,1,3).bytes(source,source.length,0);
            assertEquals(expected.capacity(),writer.size());
            assertArrayEquals(expected.array(),writer.toByteArray());
            byte[] detached=writer.toByteArray();Arrays.fill(detached,(byte)0);Arrays.fill(source,(byte)0);
            assertArrayEquals(expected.array(),writer.toByteArray());writer.finish();
            assertThrows(IllegalStateException.class,()->writer.u8(0));
            assertThrows(IllegalStateException.class,()->writer.bytes(new byte[0]));
        }
        assertArrayEquals(expected.array(),Arrays.copyOfRange(destination,4,destination.length-5));
        for(int i=0;i<4;i++)assertEquals((byte)0x5a,destination[i]);
        for(int i=destination.length-5;i<destination.length;i++)assertEquals((byte)0x5a,destination[i]);
    }

    @Test void failedPrimitiveAndBulkWritesAreAtomicAtTheRegionBoundary() {
        List<Consumer<Wire.Writer>> overflow=List.of(w->w.u16(1),w->w.i32(1),w->w.i64(1),w->w.f32(1),
            w->w.bytes(new byte[2]),w->w.bytes(new byte[4],1,2),w->w.string("a"));
        for(var write:overflow){
            byte[] destination={11,12,13,14};
            try(var writer=Wire.Writer.bounded(destination,1,1)){
                assertThrows(IllegalArgumentException.class,()->write.accept(writer));
                assertEquals(0,writer.size());assertArrayEquals(new byte[]{11,12,13,14},destination);
                writer.u8(99);writer.finish();assertArrayEquals(new byte[]{11,99,13,14},destination);
            }
        }
    }

    @Test void invalidRegionsAndSourceSlicesCannotOverwriteAnAdjacentRegion() {
        byte[] destination={11,12,13,14},source={1,2,3};
        for(int[] range:new int[][]{{-1,1},{0,-1},{4,1},{Integer.MAX_VALUE,1},{1,Integer.MAX_VALUE}})
            assertThrows(IndexOutOfBoundsException.class,()->Wire.Writer.bounded(destination,range[0],range[1]));
        try(var writer=Wire.Writer.bounded(destination,1,2)){
            for(int[] slice:new int[][]{{-1,1},{0,-1},{2,2},{Integer.MAX_VALUE,1},{1,Integer.MAX_VALUE}}){
                assertThrows(IndexOutOfBoundsException.class,()->writer.bytes(source,slice[0],slice[1]));
                assertEquals(0,writer.size());assertArrayEquals(new byte[]{11,12,13,14},destination);
            }
            for(float bad:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY}){
                assertThrows(IllegalArgumentException.class,()->writer.f32(bad));assertEquals(0,writer.size());
            }
            writer.u16(0x1234);writer.finish();
        }
        assertArrayEquals(new byte[]{11,0x34,0x12,14},destination);
    }

    @Test void underwriteAndCloseRevokeRetainedWritersIncludingEmptyRegions() {
        byte[] destination={11,12,13,14};
        var incomplete=Wire.Writer.bounded(destination,1,2);incomplete.u8(99);
        assertThrows(IllegalArgumentException.class,incomplete::finish);
        assertThrows(IllegalStateException.class,()->incomplete.u8(100));incomplete.close();
        assertArrayEquals(new byte[]{11,99,13,14},destination);
        var abandoned=Wire.Writer.bounded(destination,1,2);abandoned.close();abandoned.close();
        assertThrows(IllegalStateException.class,()->abandoned.i32(1));
        try(var empty=Wire.Writer.bounded(destination,destination.length,0)){
            assertEquals(0,empty.size());assertArrayEquals(new byte[0],empty.toByteArray());empty.finish();
            assertThrows(IllegalStateException.class,()->empty.bytes(new byte[0]));
        }
        assertArrayEquals(new byte[]{11,99,13,14},destination);
    }

    @Test void foreignThreadCannotWriteFinishCopyOrRevokeOwnersRegion()throws Exception {
        byte[] destination={11,12,13,14};
        try(var writer=Wire.Writer.bounded(destination,1,2)){
            var failure=new AtomicReference<Throwable>();
            var foreign=new Thread(()->{
                try {
                    assertThrows(IllegalStateException.class,()->writer.u8(99));
                    assertThrows(IllegalStateException.class,writer::finish);
                    assertThrows(IllegalStateException.class,writer::toByteArray);
                    assertThrows(IllegalStateException.class,writer::close);
                }catch(Throwable problem){failure.set(problem);}
            },"bounded-writer-foreign-owner");
            foreign.start();foreign.join(5000);assertFalse(foreign.isAlive());assertNull(failure.get());
            assertEquals(0,writer.size());assertArrayEquals(new byte[]{11,12,13,14},destination);
            writer.u16(0x1234);writer.finish();assertArrayEquals(new byte[]{11,0x34,0x12,14},destination);
        }
    }
}
