package dev.goldcraft.client;

import dev.goldcraft.client.mixin.ImmediateBuffersAccessor;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.SequencedMap;
import java.util.function.ToIntFunction;
import java.util.function.IntFunction;
import net.minecraft.client.render.RenderLayer;
import net.minecraft.client.render.VertexConsumerProvider;
import net.minecraft.client.util.BufferAllocator;

/** Render-thread pool retaining the live provider's fixed-layer and flush-order contract. */
final class EffectBuffers {
    private static EffectBuffers cached;
    private final BufferAllocator fallback;
    private final SequencedMap<RenderLayer,BufferAllocator> fixed;
    private final PooledImmediate provider;
    private boolean acquired,disposed;

    /** Read the original before enabling capture, so its getter is not redirected back into this pool. */
    static EffectBuffers acquire(VertexConsumerProvider.Immediate original) {
        var layers=((ImmediateBuffersAccessor)original).goldcraft$fixedLayers();
        if(cached!=null&&cached.acquired)throw new IllegalStateException("Effect buffers already acquired");
        if(cached!=null&&!cached.matches(layers)) {var stale=cached;cached=null;stale.dispose();}
        if(cached==null)cached=new EffectBuffers(layers);
        cached.acquired=true;
        return cached;
    }

    private EffectBuffers(SequencedMap<RenderLayer,BufferAllocator> layers) {
        fallback=new BufferAllocator(16384);
        SequencedMap<RenderLayer,BufferAllocator> allocated=null;
        try {
            // Use the registered keys and order, but never share native memory with the real frame.
            allocated=allocate(layers,RenderLayer::getExpectedBufferSize);
            fixed=allocated;
            provider=new PooledImmediate(fallback,fixed);
        }catch(RuntimeException|Error failure) {
            var cleanup=new CaptureCleanup(failure);
            if(allocated!=null)for(var buffer:allocated.values())cleanup.run(buffer::close);
            cleanup.run(fallback::close);
            throw failure;
        }
    }

    /** Also exercised without RenderLayer's game-registry initialization in ordinary JUnit. */
    static <K> SequencedMap<K,BufferAllocator> allocate(SequencedMap<K,?> layers,ToIntFunction<K> size) {
        return allocate(layers,size,BufferAllocator::new);
    }
    static <K> SequencedMap<K,BufferAllocator> allocate(SequencedMap<K,?> layers,ToIntFunction<K> size,IntFunction<BufferAllocator> create) {
        SequencedMap<K,BufferAllocator> result=new LinkedHashMap<>();
        try {
            for(var layer:layers.keySet()) {
                var buffer=create.apply(Math.clamp(size.applyAsInt(layer),256,16384));
                try {result.put(layer,buffer);}
                catch(RuntimeException|Error failure) {new CaptureCleanup(failure).run(buffer::close);throw failure;}
            }
            return result;
        }catch(RuntimeException|Error failure) {
            var cleanup=new CaptureCleanup(failure);
            for(var buffer:result.values())cleanup.run(buffer::close);
            throw failure;
        }
    }

    VertexConsumerProvider.Immediate provider() {
        if(!acquired||disposed)throw new IllegalStateException("Effect buffers are not acquired");
        return provider;
    }

    /** Only a completed draw may recycle buffers. Failed/unfinished frames release all owned native memory. */
    void release(boolean healthy) {
        if(!acquired)return;
        acquired=false;
        if(!healthy||!provider.drained()) {
            if(cached==this)cached=null;
            dispose();
        }
    }

    private boolean matches(SequencedMap<RenderLayer,BufferAllocator> layers) {
        if(fixed.size()!=layers.size())return false;
        Iterator<RenderLayer> current=layers.keySet().iterator();
        for(var layer:fixed.keySet())if(layer!=current.next())return false;
        return true;
    }

    private void dispose() {
        if(disposed)return;
        disposed=true;
        var cleanup=new CaptureCleanup();
        for(var buffer:fixed.values())cleanup.run(buffer::close);
        cleanup.run(fallback::close);
        cleanup.rethrow();
    }

    /** Keep vanilla getBuffer/draw/endLastBatch behavior, including simultaneous fixed-layer consumers. */
    private static final class PooledImmediate extends VertexConsumerProvider.Immediate {
        PooledImmediate(BufferAllocator fallback,SequencedMap<RenderLayer,BufferAllocator> fixed) {super(fallback,fixed);}
        boolean drained(){return pending.isEmpty()&&currentLayer==null;}
    }
}
