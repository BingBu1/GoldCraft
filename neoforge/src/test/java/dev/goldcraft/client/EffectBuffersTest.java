package dev.goldcraft.client;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.SequencedMap;
import net.minecraft.client.render.BufferBuilder;
import net.minecraft.client.render.BuiltBuffer;
import net.minecraft.client.render.VertexConsumer;
import net.minecraft.client.render.VertexConsumers;
import net.minecraft.client.render.VertexFormat;
import net.minecraft.client.render.VertexFormats;
import net.minecraft.client.util.BufferAllocator;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/** Real native allocators and union writers; no untransformed Minecraft registry bootstrap. */
class EffectBuffersTest {
    private static SequencedMap<String,Object> layers() {
        var layers=new LinkedHashMap<String,Object>();
        layers.put("glint",new Object());layers.put("base",new Object());
        return layers;
    }
    private static void quad(VertexConsumer target) {
        for(int i=0;i<4;i++)target.vertex(i,0,0).color(255,255,255,255).texture(i/3f,0);
    }
    private static BufferBuilder builder(BufferAllocator buffer) {
        return new BufferBuilder(buffer,VertexFormat.DrawMode.QUADS,VertexFormats.POSITION_TEXTURE_COLOR);
    }
    private static final class Tracked extends BufferAllocator {
        final int capacity;
        boolean closed;
        RuntimeException failure;
        Tracked(int size){super(size);capacity=size;}
        @Override public void close(){closed=true;super.close();if(failure!=null)throw failure;}
    }

    @Test void simultaneousUnionConsumersRetainIndependentMinecraftBuffers() {
        var original=layers();
        var pool=EffectBuffers.allocate(original,key->256);
        try {
            var first=builder(pool.get("glint"));var second=builder(pool.get("base"));
            quad(VertexConsumers.union(first,second));
            try(BuiltBuffer glint=first.end();BuiltBuffer base=second.end()) {
                assertEquals(4,glint.getDrawParameters().vertexCount());
                assertEquals(4,base.getDrawParameters().vertexCount());
                assertNotSame(glint.getBuffer(),base.getBuffer());
                assertEquals(glint.getBuffer(),base.getBuffer());
            }
            assertEquals(List.of("glint","base"),new ArrayList<>(pool.keySet()));
            assertEquals(2,original.size());
        }finally {pool.values().forEach(BufferAllocator::close);}
    }

    @Test void finishingOneBuilderBeforeTheUnionReproducesTheRejectedFallbackPattern() {
        try(var firstMemory=new BufferAllocator(256);var secondMemory=new BufferAllocator(256)) {
            var first=builder(firstMemory);var second=builder(secondMemory);
            // A non-fixed Immediate provider calls endNullable when switching layers.
            assertNull(first.endNullable());
            assertThrows(IllegalStateException.class,()->quad(VertexConsumers.union(first,second)));
        }
    }

    @Test void registrationOrderAndBoundedInitialCapacityAreRetained() {
        var original=layers();original.putFirst("base",original.get("base"));
        var created=new ArrayList<Tracked>();
        var pool=EffectBuffers.allocate(original,key->key.equals("base")?1:Integer.MAX_VALUE,size->{
            var buffer=new Tracked(size);created.add(buffer);return buffer;
        });
        try {
            assertEquals(List.of("base","glint"),new ArrayList<>(pool.keySet()));
            assertEquals(List.of(256,16384),created.stream().map(b->b.capacity).toList());
        }finally {pool.values().forEach(BufferAllocator::close);}
        assertTrue(created.stream().allMatch(b->b.closed));
    }

    @Test void failedAllocationClosesEarlierBuffersAndPreservesTheCause() {
        var failure=new IllegalStateException("allocation failed");
        var cleanupFailure=new IllegalStateException("close failed");
        var created=new ArrayList<Tracked>();
        var input=layers();input.put("third",new Object());
        var result=assertThrows(IllegalStateException.class,()->EffectBuffers.allocate(input,key->256,size->{
            if(created.size()==2)throw failure;
            var buffer=new Tracked(size);if(created.isEmpty())buffer.failure=cleanupFailure;created.add(buffer);return buffer;
        }));
        assertSame(failure,result);assertArrayEquals(new Throwable[]{cleanupFailure},result.getSuppressed());
        assertEquals(2,created.size());assertTrue(created.stream().allMatch(b->b.closed));
    }
}
