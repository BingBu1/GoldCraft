package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
import java.util.Objects;
import java.util.function.Consumer;

/** Protocol26 publishes animated entity depth owners and their effects together. */
public final class EntityFrame {
    private EntityFrame() {}
    /** Writes synchronously while its collectors remain owned by the current frame. */
    public record Base(int size,Consumer<Wire.Writer> write) {
        public Base {
            if(size<32)throw new IllegalArgumentException("Truncated entity base");
            Objects.requireNonNull(write);
        }
    }
    public static byte[] encode(long epoch,long generation,long revision,int life,byte[] base,List<ParticleSnapshot.Batch> effects) {
        if(base.length<32)throw new IllegalArgumentException("Truncated entity base");
        var header=ByteBuffer.wrap(base).order(ByteOrder.LITTLE_ENDIAN);
        if(header.getLong()!=epoch||header.getLong()!=revision)throw new IllegalArgumentException("Entity frame identity mismatch");
        return encode(epoch,generation,revision,life,new Base(base.length,out->out.bytes(base)),effects);
    }
    public static byte[] encode(long epoch,long generation,long revision,int life,Base base,List<ParticleSnapshot.Batch> effects) {
        int count=effects.isEmpty()?0:1;
        int meshSize=ParticleSnapshot.encodedSize(epoch,generation,revision,life,count,effects);
        long size=8L+base.size()+meshSize;
        if(size>Wire.MAX_PAYLOAD)throw new IllegalArgumentException("Entity frame byte budget");
        var batches=List.copyOf(effects);
        byte[] frame=new byte[(int)size];
        var header=ByteBuffer.wrap(frame).order(ByteOrder.LITTLE_ENDIAN);
        header.putInt(base.size());
        try(var out=Wire.Writer.bounded(frame,4,base.size())){
            base.write().accept(out);out.finish();
        }
        if(header.getLong(4)!=epoch||header.getLong(12)!=revision)throw new IllegalArgumentException("Entity frame identity mismatch");
        header.putInt(4+base.size(),meshSize);
        ParticleSnapshot.encodeInto(epoch,generation,revision,life,count,batches,frame,8+base.size(),meshSize);
        return frame;
    }
}
