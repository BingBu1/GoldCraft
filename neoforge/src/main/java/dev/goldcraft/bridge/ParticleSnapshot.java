package dev.goldcraft.bridge;

import java.util.List;

/** Independent snapshots can replace queued frames without losing particle removals. */
public final class ParticleSnapshot {
    public static final int MAX_PARTICLES=4096,MAX_VERTICES=65536,MAX_BATCHES=128,MAX_TEXTURES=64;
    public record Batch(int texture,int flags,byte[] vertices) {}
    private ParticleSnapshot() {}
    public static byte[] encode(long epoch,long generation,long revision,int life,int count,List<Batch> batches) {
        if(epoch==0||generation<=0||revision<=0||life==0||count<0||count>MAX_PARTICLES||batches.size()>MAX_BATCHES
            ||(count==0)!=batches.isEmpty())throw new IllegalArgumentException("Particle identity/count bounds");
        Wire.Writer out=new Wire.Writer().i64(epoch).i64(generation).i64(revision).i32(life).i32(count).i32(batches.size());
        int total=0;
        for(Batch b:batches){
            if(b.texture<0||b.texture>MAX_TEXTURES||(b.flags&~1)!=0||b.vertices.length==0||b.vertices.length%96!=0)
                throw new IllegalArgumentException("Particle batch bounds");
            int vertices=b.vertices.length/24;total+=vertices;
            if(total>MAX_VERTICES)throw new IllegalArgumentException("Particle vertex budget");
            out.i32(b.texture).i32(b.flags).i32(vertices).bytes(b.vertices);
        }
        return out.toByteArray();
    }
}
