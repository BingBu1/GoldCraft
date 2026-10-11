package dev.goldcraft.client;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.util.Identifier;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class EntityCollectorBuffersTest {
    private static byte[] storage(WorldExporter.Collector collector)throws Exception {
        var field=WorldExporter.Collector.class.getDeclaredField("vertices");field.setAccessible(true);return (byte[])field.get(collector);
    }
    private static void vertices(WorldExporter.Collector collector,int count){
        for(int i=0;i<count;i++)collector.vertex(i,64,-2);
    }
    private static byte[] defaultQuad(boolean triangles){
        var bytes=ByteBuffer.allocate(112).order(ByteOrder.LITTLE_ENDIAN);
        for(int i=0;i<4;i++)bytes.putFloat(triangles&&i==3?2:i).putFloat(64).putFloat(-2)
            .putFloat(0).putFloat(0).putInt(-1).putInt(0);
        return bytes.array();
    }
    @Test void firstActualWriteBorrowsArrayButEveryFrameHasFreshConsumerAndDefaults()throws Exception {
        for(boolean triangles:new boolean[]{false,true}){
            var pool=new EntityCollectorBuffers<String>();WorldExporter.Collector stale;byte[] original;
            try(var frame=pool.beginFrame()){
                stale=frame.collector("skin",triangles);assertEquals(0,storage(stale).length);
                assertEquals(0,stale.finishSize());assertEquals(0,pool.misses());
                stale.offset(13,27,-91);stale.color(2,3,4,5).texture(7,9).overlay(15,0);
                vertices(stale,triangles?3:4);stale.finishSize();original=storage(stale);
                assertEquals(1,pool.misses());frame.complete();
            }
            assertNull(storage(stale));assertEquals(original.length,pool.retainedBytes());
            try(var frame=pool.beginFrame()){
                var next=frame.collector("skin",triangles);assertNotSame(stale,next);assertEquals(0,pool.reuses());
                next.vertex(0,64,-2);assertEquals(0,pool.reuses()); // First vertex remains pending.
                for(int i=1;i<(triangles?3:4);i++)next.vertex(i,64,-2);
                assertArrayEquals(defaultQuad(triangles),next.finish());assertSame(original,storage(next));
                assertEquals(0,next.hurtVertices());assertEquals(1,pool.reuses());frame.complete();
            }
        }
    }
    @Test void completeProductionMaterialKeySeparatesAllStatesAndPreservesCurrentFrameOrder()throws Exception {
        var pool=new EntityCollectorBuffers<EntityExporter.Material>();
        var keys=new ArrayList<EntityExporter.Material>();var arrays=new ArrayList<byte[]>();
        for(int texture=0;texture<2;texture++)for(int flags=0;flags<16;flags++)
            keys.add(new EntityExporter.Material(Identifier.ofVanilla("textures/skin"+texture+".png"),
                (flags&1)!=0,(flags&2)!=0,(flags&4)!=0,(flags&8)!=0));
        try(var frame=pool.beginFrame()){
            for(var key:keys){
                var collector=frame.collector(key,key.triangles());
                assertSame(collector,frame.collector(key,key.triangles()));
                vertices(collector,key.triangles()?3:4);collector.finishSize();arrays.add(storage(collector));
            }
            assertEquals(keys,new ArrayList<>(frame.collectors().keySet()));frame.complete();
        }
        try(var frame=pool.beginFrame()){
            for(int i=keys.size()-1;i>=0;i--){
                var key=keys.get(i);var collector=frame.collector(key,key.triangles());
                vertices(collector,key.triangles()?3:4);
                assertArrayEquals(defaultQuad(key.triangles()),collector.finish());assertSame(arrays.get(i),storage(collector));
            }
            assertEquals(keys.reversed(),new ArrayList<>(frame.collectors().keySet()));frame.complete();
        }
        assertEquals(keys.size(),pool.reuses());
    }
    @Test void everyStaleConsumerAndProviderEntryPointIsClosed()throws Exception {
        var pool=new EntityCollectorBuffers<String>();var frame=pool.beginFrame();
        var stale=frame.collector("skin",false);vertices(stale,4);stale.finishSize();frame.complete();frame.close();
        assertThrows(IllegalStateException.class,()->frame.collector("skin",false));
        assertThrows(IllegalStateException.class,()->frame.collector("different",true));
        assertThrows(IllegalStateException.class,frame::requireOpen);
        assertThrows(IllegalStateException.class,frame::collectors);
        assertThrows(IllegalStateException.class,frame::complete);
        assertThrows(IllegalStateException.class,()->stale.vertex(0,0,0));
        assertThrows(IllegalStateException.class,()->stale.offset(0,0,0));
        assertThrows(IllegalStateException.class,()->stale.color(1,2,3,4));
        assertThrows(IllegalStateException.class,()->stale.texture(0,0));
        assertThrows(IllegalStateException.class,()->stale.overlay(0,0));
        assertThrows(IllegalStateException.class,()->stale.light(0,0));
        assertThrows(IllegalStateException.class,()->stale.normal(0,0,0));
        assertThrows(IllegalStateException.class,stale::finishSize);
        assertThrows(IllegalStateException.class,stale::finish);
        assertThrows(IllegalStateException.class,()->stale.writeTo(new dev.goldcraft.bridge.Wire.Writer(),112));
        frame.close();assertEquals(1,pool.retainedEntries());assertNull(storage(stale));
    }
    @Test void failuresEarlyReturnsAndResetDropArraysWithoutFlushingPendingState()throws Exception {
        var pool=new EntityCollectorBuffers<String>();
        try(var frame=pool.beginFrame()){
            vertices(frame.collector("cached",false),4);frame.collectors().get("cached").finishSize();frame.complete();
        }
        for(int failure=0;failure<3;failure++){
            WorldExporter.Collector invalid;
            try(var frame=pool.beginFrame()){
                invalid=frame.collector("bad",false);
                if(failure==0){vertices(invalid,3);assertThrows(IllegalArgumentException.class,invalid::finishSize);}
                else if(failure==1){invalid.vertex(Float.NaN,0,0);assertThrows(IllegalArgumentException.class,invalid::finishSize);}
                else vertices(invalid,4); // Early return before serialization; close must not flush.
            }
            assertNull(storage(invalid));assertEquals(0,pool.retainedEntries());assertEquals(0,pool.retainedBytes());
            try(var recovery=pool.beginFrame()){
                var fresh=recovery.collector("bad",false);vertices(fresh,4);
                assertArrayEquals(defaultQuad(false),fresh.finish());recovery.complete();
            }
        }
        var active=pool.beginFrame();var stale=active.collector("bad",false);vertices(stale,4);stale.finishSize();active.complete();
        pool.clear();assertNull(storage(stale));assertEquals(0,pool.retainedBytes());
        assertThrows(IllegalStateException.class,active::complete);active.close();assertEquals(0,pool.retainedEntries());
        try(var recovery=pool.beginFrame()){
            var fresh=recovery.collector("bad",false);vertices(fresh,4);assertArrayEquals(defaultQuad(false),fresh.finish());recovery.complete();
        }
        pool.clear();pool.clear();assertEquals(0,pool.retainedBytes());assertEquals(0,pool.retainedEntries());
    }
    @Test void cleanupDuringThrownErrorAndNestedAcquisitionDoesNotMaskOriginalFailure() {
        var pool=new EntityCollectorBuffers<String>();var original=new AssertionError("renderer failed");
        var actual=assertThrows(AssertionError.class,()->{
            try(var frame=pool.beginFrame()){
                assertThrows(IllegalStateException.class,pool::beginFrame);
                vertices(frame.collector("skin",false),3);throw original;
            }
        });
        assertSame(original,actual);assertEquals(0,actual.getSuppressed().length);
        assertEquals(0,pool.retainedEntries());try(var frame=pool.beginFrame()){frame.complete();}
    }
    @Test void deferredEffectsCloseBeforeBufferReleaseAndFailedClosePreventsRetention()throws Exception {
        var pool=new EntityCollectorBuffers<String>();var original=new IllegalStateException("effect cleanup failed");
        WorldExporter.Collector[] captured=new WorldExporter.Collector[1];
        assertSame(original,assertThrows(IllegalStateException.class,()->{
            try(var frame=pool.beginFrame()){
                try(AutoCloseable effects=()->{
                    // Match the production unwind order: effects can still
                    // touch their frame's consumers until their close returns.
                    assertEquals(112,captured[0].finishSize());throw original;
                }){
                    captured[0]=frame.collector("skin",false);vertices(captured[0],4);captured[0].finishSize();
                }
                frame.complete();
            }
        }));
        assertNull(storage(captured[0]));assertEquals(0,pool.retainedBytes());
    }
    @Test void failedRetentionStillDetachesEveryConsumerAndPreservesFirstFailure()throws Exception {
        class Key {
            final int value;RuntimeException nextFailure;int hashes;
            Key(int value){this.value=value;}
            @Override public int hashCode(){
                hashes++;var failure=nextFailure;nextFailure=null;
                if(failure!=null)throw failure;return value;
            }
        }
        var pool=new EntityCollectorBuffers<Key>();var frame=pool.beginFrame();
        var keys=List.of(new Key(1),new Key(2),new Key(3));
        var consumers=new ArrayList<WorldExporter.Collector>();
        for(var key:keys){var collector=frame.collector(key,false);vertices(collector,4);collector.finishSize();consumers.add(collector);}
        frame.complete();int lastHashes=keys.getLast().hashes;
        var first=new IllegalStateException("first retention failure");var second=new IllegalArgumentException("second retention failure");
        keys.get(0).nextFailure=first;keys.get(1).nextFailure=second;
        assertSame(first,assertThrows(IllegalStateException.class,frame::close));
        assertArrayEquals(new Throwable[]{second},first.getSuppressed());
        assertTrue(keys.getLast().hashes>lastHashes);
        for(var consumer:consumers){assertNull(storage(consumer));assertThrows(IllegalStateException.class,consumer::finishSize);}
        assertEquals(0,pool.retainedBytes());assertEquals(0,pool.retainedEntries());frame.close();
        try(var recovery=pool.beginFrame()){
            var collector=recovery.collector(keys.getFirst(),false);vertices(collector,4);
            assertArrayEquals(defaultQuad(false),collector.finish());recovery.complete();
        }
        assertEquals(1,pool.retainedEntries());
    }
    @Test void totalRetainedBytesAndEntriesStayBoundedAcrossMaterialChurn() {
        var pool=new EntityCollectorBuffers<String>();
        for(int key=0;key<3;key++){
            // Each individual frame remains within the real 8 MiB mesh budget.
            try(var frame=pool.beginFrame()){
                var collector=frame.collector("large"+key,false);vertices(collector,262144);collector.finishSize();frame.complete();
            }
            assertTrue(pool.retainedBytes()<=EntityCollectorBuffers.MAX_RETAINED_BYTES);
        }
        assertEquals(2,pool.retainedEntries());assertEquals(2*262144*28,pool.retainedBytes());assertEquals(1,pool.evictions());
        pool.clear();
        for(int key=0;key<129;key++){
            try(var frame=pool.beginFrame()){
                var collector=frame.collector("small"+key,false);vertices(collector,4);collector.finishSize();frame.complete();
            }
            assertTrue(pool.retainedEntries()<=EntityCollectorBuffers.MAX_RETAINED_ENTRIES);
            assertTrue(pool.retainedBytes()<=EntityCollectorBuffers.MAX_RETAINED_BYTES);
        }
        assertEquals(128,pool.retainedEntries());assertEquals(128*4096,pool.retainedBytes());
        long misses=pool.misses();
        try(var frame=pool.beginFrame()){
            var evicted=frame.collector("small0",false);vertices(evicted,4);evicted.finishSize();frame.complete();
        }
        assertEquals(misses+1,pool.misses());
    }
    @Test void emptyRequestsDoNotBorrowAndActiveWorkIsNotRestrictedByRetentionBudget() {
        var pool=new EntityCollectorBuffers<String>();
        try(var frame=pool.beginFrame()){
            for(int i=0;i<512;i++)assertEquals(0,frame.collector("empty"+i,false).finishSize());
            frame.complete();
        }
        assertEquals(0,pool.misses());assertEquals(0,pool.retainedBytes());assertEquals(0,pool.retainedEntries());
        // A formerly large buffer may now serve a small material. Retained
        // capacity is never used as a new gameplay/geometry rejection rule.
        for(String key:List.of("a","b"))try(var frame=pool.beginFrame()){
            var collector=frame.collector(key,false);vertices(collector,262144);collector.finishSize();frame.complete();
        }
        try(var frame=pool.beginFrame()){
            for(String key:List.of("a","b")){var collector=frame.collector(key,false);vertices(collector,4);collector.finishSize();}
            var large=frame.collector("c",false);vertices(large,262144);large.finishSize();
            var batches=frame.collectors().values().stream().map(c->new EntityExporter.Batch(0,2,c,c.finishSize())).toList();
            assertTrue(EntityExporter.encodeBase(1,1,1,0,batches,List.of()).length<8*1024*1024);frame.complete();
        }
        assertTrue(pool.retainedBytes()<=EntityCollectorBuffers.MAX_RETAINED_BYTES);
    }
}
