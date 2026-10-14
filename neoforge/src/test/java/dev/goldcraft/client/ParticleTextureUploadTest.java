package dev.goldcraft.client;

import dev.goldcraft.bridge.ParticleTextureChunks;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ParticleTextureUploadTest {
    @Test void full4096AtlasTakes64BoundedFramesAndReconstructsExactly(){
        var h=new ParticleTextureChunks.Header(7,8,9,10,11,4096,4096);
        var source=ByteBuffer.allocate(h.totalBytes());
        for(int i=0;i<source.capacity();i++)source.put(i,(byte)(i*31+(i>>>16)));
        AtomicInteger freed=new AtomicInteger(),accepted=new AtomicInteger();
        try(var upload=new ParticleTextureUpload(h,21,99,source,freed::incrementAndGet)) {
            int frames=0;
            boolean done;
            do {
                int before=accepted.get();
                done=upload.pump(packet->{
                    var b=ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN);int offset=b.getInt(40);
                    assertEquals(accepted.get(),offset);
                    for(int i=48;i<packet.length;i++)assertEquals(source.get(offset+i-48),packet[i]);
                    accepted.addAndGet(packet.length-48);return true;
                });
                assertTrue(accepted.get()-before<=1024*1024);frames++;
            }while(!done);
            assertEquals(64,frames);assertEquals(h.totalBytes(),accepted.get());
            assertEquals(21,upload.texture());assertEquals(99,upload.stamp());
            assertEquals(11,upload.header().revision());assertEquals(0,freed.get());
        }
        assertEquals(1,freed.get());
    }
    @Test void queueBackpressureRetainsSameBlockAndMakesNoProgress(){
        var h=new ParticleTextureChunks.Header(1,2,3,4,5,4096,128);
        AtomicInteger freed=new AtomicInteger();
        var upload=new ParticleTextureUpload(h,21,7,ByteBuffer.allocate(h.totalBytes()),freed::incrementAndGet);
        AtomicReference<byte[]> first=new AtomicReference<>();
        assertFalse(upload.pump(packet->{first.set(packet);return false;}));
        assertEquals(0,upload.sentBytes());
        assertFalse(upload.pump(packet->{assertSame(first.get(),packet);return false;}));
        List<Integer> offsets=new ArrayList<>();
        assertFalse(upload.pump(packet->{offsets.add(ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN).getInt(40));return true;}));
        assertEquals(List.of(0,262144,524288,786432),offsets);assertEquals(1048576,upload.sentBytes());
        assertTrue(upload.pump(packet->true));upload.close();upload.close();assertEquals(1,freed.get());
        assertThrows(IllegalStateException.class,()->upload.pump(packet->true));
    }
    @Test void lifetimeIdentityAndCancellationKeepCapturedStampIndependent(){
        var h=new ParticleTextureChunks.Header(1,2,3,4,50,4,4);AtomicInteger freed=new AtomicInteger();
        var upload=new ParticleTextureUpload(h,21,7,ByteBuffer.allocate(64),freed::incrementAndGet);
        assertTrue(upload.matches(1,2,3));assertFalse(upload.matches(2,2,3));
        assertFalse(upload.matches(1,3,3));assertFalse(upload.matches(1,2,4));
        assertTrue(upload.matchesResources(1,2));
        assertFalse(upload.matchesResources(2,2));assertFalse(upload.matchesResources(1,3));
        // Content changes after capture cannot rewrite the in-flight snapshot's stamp.
        assertEquals(7,upload.stamp());upload.close();assertEquals(1,freed.get());
    }
    @Test void newAvatarLifeDoesNotRestartImmutableTextureBytes(){
        var h=new ParticleTextureChunks.Header(1,2,3,4,50,4096,128);
        var released=new AtomicInteger();
        try(var upload=new ParticleTextureUpload(h,21,7,ByteBuffer.allocate(h.totalBytes()),released::incrementAndGet)){
            assertFalse(upload.pump(packet->true));
            assertEquals(1048576,upload.sentBytes());
            assertFalse(upload.matches(1,2,4));assertTrue(upload.matchesResources(1,2));
            assertTrue(upload.pump(packet->{
                var reader=ByteBuffer.wrap(packet).order(ByteOrder.LITTLE_ENDIAN);
                assertEquals(3,reader.getInt(16));
                assertTrue(reader.getInt(40)>=1048576);return true;
            }));
        }
        assertEquals(1,released.get());
    }
}
