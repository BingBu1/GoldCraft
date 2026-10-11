package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HexFormat;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ParticleTextureChunksTest {
    @Test void exactLittleEndianHeaderAndSourcePositionArePreserved(){
        var header=new ParticleTextureChunks.Header(0x0102030405060708L,9,10,11,12,3,2);
        var pixels=ByteBuffer.allocate(29);pixels.position(5);
        for(int i=0;i<24;i++)pixels.put(5+i,(byte)i);
        var encoded=ParticleTextureChunks.encode(header,0,pixels);
        assertEquals("080706050403020109000000000000000a0000000b0000000c0000000000000003000000020000000000000018000000",
            HexFormat.of().formatHex(encoded,0,48));
        assertEquals(72,encoded.length);assertEquals(5,pixels.position());
        for(int i=0;i<24;i++)assertEquals((byte)i,encoded[48+i]);
    }
    @Test void nonPowerOfTwoWidthsUseWholeRowsAndExactFinalLength(){
        var header=new ParticleTextureChunks.Header(1,2,3,4,5,3001,73);
        var pixels=ByteBuffer.allocate(header.totalBytes());int offset=0,chunks=0;
        while(offset<header.totalBytes()) {
            var packet=ParticleTextureChunks.encode(header,offset,pixels);
            var bytes=ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN);
            assertEquals(offset,bytes.getInt(40));assertEquals(header.totalBytes(),bytes.getInt(44));
            int length=packet.length-48;assertTrue(length<=256*1024);assertEquals(0,length%(3001*4));
            offset+=length;chunks++;
        }
        assertEquals(header.totalBytes(),offset);assertEquals(4,chunks);
    }
    @Test void invalidIdentityDimensionsOffsetsAndStorageAreRejectedBeforeCopy(){
        assertThrows(IllegalArgumentException.class,()->new ParticleTextureChunks.Header(0,2,3,4,5,1,1));
        assertThrows(IllegalArgumentException.class,()->new ParticleTextureChunks.Header(1,2,0,4,5,1,1));
        assertThrows(IllegalArgumentException.class,()->new ParticleTextureChunks.Header(1,2,3,65,5,1,1));
        assertThrows(IllegalArgumentException.class,()->new ParticleTextureChunks.Header(1,2,3,4,0,1,1));
        assertThrows(IllegalArgumentException.class,()->new ParticleTextureChunks.Header(1,2,3,4,5,4097,1));
        var header=new ParticleTextureChunks.Header(1,2,3,4,5,2,2);
        assertThrows(IllegalArgumentException.class,()->ParticleTextureChunks.encode(header,0,ByteBuffer.allocate(15)));
        for(int offset:new int[]{-8,1,4,16,Integer.MAX_VALUE})
            assertThrows(IllegalArgumentException.class,()->ParticleTextureChunks.encode(header,offset,ByteBuffer.allocate(16)));
    }
}
