package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.nio.ByteBuffer;
import java.util.UUID;
import static org.junit.jupiter.api.Assertions.*;

class WireTest {
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
