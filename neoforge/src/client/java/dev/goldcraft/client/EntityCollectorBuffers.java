package dev.goldcraft.client;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** Render-thread scratch arrays. The limits cover retained storage, not active frames or the JVM heap. */
final class EntityCollectorBuffers<K> {
    static final int MAX_RETAINED_BYTES=16*1024*1024,MAX_RETAINED_ENTRIES=128;
    private final LinkedHashMap<K,byte[]> cached=new LinkedHashMap<>();
    private int retainedBytes;
    private long reuses,misses,evictions;
    private Frame active;

    int retainedBytes(){return retainedBytes;}
    int retainedEntries(){return cached.size();}
    long reuses(){return reuses;}
    long misses(){return misses;}
    long evictions(){return evictions;}
    Frame beginFrame(){
        if(active!=null)throw new IllegalStateException("Entity collector frame already active");
        active=new Frame();return active;
    }
    void clear(){
        clearCached();
        if(active!=null)active.release(false);
    }
    private void clearCached(){cached.clear();retainedBytes=0;}
    private byte[] take(K key){
        byte[] bytes=cached.remove(key);
        if(bytes!=null){retainedBytes-=bytes.length;reuses++;}else misses++;
        return bytes;
    }
    private void retain(K key,byte[] bytes){
        if(bytes.length==0||bytes.length>MAX_RETAINED_BYTES)return;
        byte[] previous=cached.remove(key);if(previous!=null)retainedBytes-=previous.length;
        while(cached.size()>=MAX_RETAINED_ENTRIES||bytes.length>MAX_RETAINED_BYTES-retainedBytes){
            var first=cached.pollFirstEntry();retainedBytes-=first.getValue().length;evictions++;
        }
        cached.put(key,bytes);retainedBytes+=bytes.length;
    }

    final class Frame implements AutoCloseable {
        private final LinkedHashMap<K,WorldExporter.Collector> collectors=new LinkedHashMap<>();
        private boolean completed,closed;
        void requireOpen(){if(closed||active!=this)throw new IllegalStateException("Closed entity collector frame");}
        WorldExporter.Collector collector(K key,boolean triangles){
            requireOpen();
            // The owner supplies its complete material key, including topology.
            // Keep a fresh consumer's defaults and do not let old renderer-held
            // references share a live consumer with a later frame.
            return collectors.computeIfAbsent(key,ignored->new WorldExporter.Collector(true,triangles,()->{
                requireOpen();return take(key);
            }));
        }
        Map<K,WorldExporter.Collector> collectors(){requireOpen();return Collections.unmodifiableMap(collectors);}
        void complete(){requireOpen();completed=true;}
        @Override public void close(){release(completed);}
        private void release(boolean healthy){
            if(closed)return;
            closed=true;if(active==this)active=null;
            var cleanup=new CaptureCleanup();
            try {
                for(var entry:collectors.entrySet()){
                    // Detach even unfinished/invalid pending vertices. Cleanup
                    // must neither flush them nor leave a stale consumer usable.
                    byte[] bytes=entry.getValue().releaseBuffer();
                    if(healthy)cleanup.run(()->retain(entry.getKey(),bytes));
                }
                cleanup.rethrow();
            }catch(RuntimeException|Error failure){clearCached();throw failure;}
            finally {collectors.clear();if(!healthy)clearCached();}
        }
    }
}
