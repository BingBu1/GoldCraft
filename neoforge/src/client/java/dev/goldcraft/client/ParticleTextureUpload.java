package dev.goldcraft.client;

import dev.goldcraft.bridge.ParticleTextureChunks;
import java.nio.ByteBuffer;
import java.util.Objects;
import java.util.function.Predicate;

/** One immutable readback, at most four 256 KiB chunks per presentation frame. */
final class ParticleTextureUpload implements AutoCloseable {
    static final int CHUNKS_PER_FRAME=4;
    private final ParticleTextureChunks.Header header;
    private final int texture;
    private final long stamp;
    private final Runnable release;
    private ByteBuffer pixels;
    private byte[] encoded;
    private int offset;
    private boolean closed;

    ParticleTextureUpload(ParticleTextureChunks.Header header,int texture,long stamp,ByteBuffer pixels,Runnable release) {
        if(texture<=0||stamp<=0||pixels.remaining()!=header.totalBytes())throw new IllegalArgumentException("Particle texture readback");
        this.header=header;this.texture=texture;this.stamp=stamp;this.pixels=pixels.asReadOnlyBuffer();this.release=Objects.requireNonNull(release);
    }
    ParticleTextureChunks.Header header(){return header;}
    int texture(){return texture;}
    long stamp(){return stamp;}
    int sentBytes(){return offset;}
    boolean matches(long epoch,long generation,int life){return header.epoch()==epoch&&header.generation()==generation&&header.life()==life;}
    boolean pump(Predicate<byte[]> send) {
        if(closed)throw new IllegalStateException("Closed particle texture upload");
        for(int i=0;i<CHUNKS_PER_FRAME&&offset<header.totalBytes();i++) {
            if(encoded==null)encoded=ParticleTextureChunks.encode(header,offset,pixels);
            // Retain exactly the same encoded block if the bounded bridge queue is full.
            if(!send.test(encoded))return false;
            offset+=encoded.length-ParticleTextureChunks.HEADER_BYTES;encoded=null;
        }
        return offset==header.totalBytes();
    }
    @Override public void close() {
        if(closed)return;
        closed=true;pixels=null;encoded=null;release.run();
    }
}
