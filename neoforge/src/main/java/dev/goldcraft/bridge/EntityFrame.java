package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;

/** Protocol26 publishes animated entity depth owners and their effects together. */
public final class EntityFrame {
    private EntityFrame() {}
    public static byte[] encode(long epoch,long generation,long revision,int life,byte[] base,List<ParticleSnapshot.Batch> effects) {
        if(base.length<32)throw new IllegalArgumentException("Truncated entity base");
        var header=ByteBuffer.wrap(base).order(ByteOrder.LITTLE_ENDIAN);
        if(header.getLong()!=epoch||header.getLong()!=revision)throw new IllegalArgumentException("Entity frame identity mismatch");
        byte[] mesh=ParticleSnapshot.encode(epoch,generation,revision,life,effects.isEmpty()?0:1,effects);
        long size=8L+base.length+mesh.length;
        if(size>Wire.MAX_PAYLOAD)throw new IllegalArgumentException("Entity frame byte budget");
        return ByteBuffer.allocate((int)size).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(base.length).put(base).putInt(mesh.length).put(mesh).array();
    }
}
