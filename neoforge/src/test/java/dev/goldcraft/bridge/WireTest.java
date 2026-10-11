package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.nio.ByteBuffer;
import java.util.UUID;
import static org.junit.jupiter.api.Assertions.*;

class WireTest {
    @Test void capacityAndByteSlicesPreserveBytesAndOwnership() {
        byte[] source={9,1,2,3,8};
        for(int capacity:new int[]{0,1,7,4096}){
            var writer=new Wire.Writer(capacity).i32(0x12345678).bytes(source,1,3).bytes(source,source.length,0);
            byte[] expected={0x78,0x56,0x34,0x12,1,2,3};
            assertArrayEquals(expected,writer.toByteArray());
            byte[] returned=writer.toByteArray();returned[0]=0;
            assertArrayEquals(expected,writer.toByteArray());
            writer.u8(7);assertEquals(7,returned.length);assertEquals(8,writer.toByteArray().length);
        }
        var writer=new Wire.Writer(3).bytes(source,1,3);source[2]=99;
        assertArrayEquals(new byte[]{1,2,3},writer.toByteArray());
        for(int[] slice:new int[][]{{-1,1},{0,-1},{4,2},{Integer.MAX_VALUE,1},{1,Integer.MAX_VALUE}}){
            assertThrows(IndexOutOfBoundsException.class,()->writer.bytes(source,slice[0],slice[1]));
            assertArrayEquals(new byte[]{1,2,3},writer.toByteArray());
        }
        assertThrows(IllegalArgumentException.class,()->new Wire.Writer(-1));
    }
    @Test void primitivesPreserveExactLittleEndianValues() {
        byte[] bytes=new Wire.Writer().u8(0xfe).u16(0x1234).i32(0x89abcdef).i32(-100).i64(0x1122334455667788L).f32(-12.5f).string("goldcraft").toByteArray();
        Wire.Reader r=new Wire.Reader(bytes);
        assertEquals(0xfe,r.u8()); assertEquals(0x1234,r.u16()); assertEquals(0x89abcdef,r.i32()); assertEquals(-100,r.i32());
        assertEquals(0x1122334455667788L,r.i64()); assertEquals(-12.5f,r.f32()); assertEquals("goldcraft",r.string(128)); r.finish();
        assertThrows(IllegalArgumentException.class,r::u8);
    }
    @Test void rejectsTruncatedOversizedAndNonFiniteData() {
        ByteBuffer good=Wire.frame(Wire.INPUT,0x1122334455667788L,2,new byte[4]);
        for(int size=0;size<Wire.HEADER_BYTES;size++) {
            ByteBuffer shortHeader=good.slice(0,size);
            assertThrows(IllegalArgumentException.class,()->Wire.header(shortHeader));
        }
        byte[] tooBig=good.array().clone(); tooBig[8]=(byte)0xff; tooBig[9]=(byte)0xff; tooBig[10]=(byte)0xff; tooBig[11]=0x7f;
        assertThrows(IllegalArgumentException.class,()->Wire.header(ByteBuffer.wrap(tooBig,0,Wire.HEADER_BYTES)));
        assertThrows(IllegalArgumentException.class,()->new Wire.Writer().f32(Float.NaN));
        assertThrows(IllegalArgumentException.class,()->new Wire.Reader(new Wire.Writer().i32(0x7f800000).toByteArray()).f32());
    }
    @Test void uuidAndSessionKeysDoNotDependOnNativePointerSize() {
        UUID uuid=UUID.fromString("00112233-4455-6677-8899-aabbccddeeff");
        assertArrayEquals(Wire.key("00112233445566778899aabbccddeeff"),Wire.uuid(uuid));
        assertEquals(uuid,Wire.uuid(Wire.uuid(uuid)));
        assertThrows(IllegalArgumentException.class,()->Wire.key("0011"));
    }
}
