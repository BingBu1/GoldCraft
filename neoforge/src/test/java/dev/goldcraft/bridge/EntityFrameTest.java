package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class EntityFrameTest {
    static byte[] base(long epoch,long revision,float x,boolean empty) {
        var out=new Wire.Writer().i64(epoch).i64(revision).i32(empty?0:1).i32(0).i32(empty?0:1);
        if(!empty){
            out.i32(1).i32(4).i32(4);
            for(int i=0;i<4;i++)out.f32(x+i).f32(64).f32(-2).f32(0).f32(1).i32(-1).i32(0x00800000);
        }
        return out.i32(0).toByteArray();
    }
    static byte[] frame(long revision,float x,boolean empty) {
        return EntityFrame.encode(9,2,revision,7,base(9,revision,x,empty),empty?List.of():List.of(glint(x)));
    }
    static ParticleSnapshot.Batch glint(float x) {
        var out=new Wire.Writer();for(int i=0;i<4;i++)out.f32(x+i).f32(64).f32(-2).f32(2).f32(-1).i32(-1);
        return new ParticleSnapshot.Batch(3,new ParticleSnapshot.Material(6|128|256,768,1,0,1,32774,32774,514,15,0,1,1,1,1),out.toByteArray());
    }
    static void assertPair(byte[] bytes,long revision,float x,boolean empty) {
        var packet=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        int baseSize=packet.getInt();assertEquals(empty?32:156,baseSize);
        assertEquals(9,packet.getLong());assertEquals(revision,packet.getLong());
        assertEquals(empty?0:1,packet.getInt());assertEquals(0,packet.getInt());assertEquals(empty?0:1,packet.getInt());
        if(!empty){assertEquals(1,packet.getInt());assertEquals(4,packet.getInt());assertEquals(4,packet.getInt());assertEquals(x,packet.getFloat());}
        packet.position(4+baseSize);int effectSize=packet.getInt();assertEquals(packet.remaining(),effectSize);
        assertEquals(9,packet.getLong());assertEquals(2,packet.getLong());assertEquals(revision,packet.getLong());
        assertEquals(7,packet.getInt());assertEquals(empty?0:1,packet.getInt());assertEquals(empty?0:1,packet.getInt());
        if(!empty){
            assertEquals(3,packet.getInt());assertEquals(6|128|256,packet.getInt());
            packet.position(8+baseSize+36+64);assertEquals(x,packet.getFloat());
        }
    }
    @Test void baseAndEqualDepthGlintShareIdentityAndDistinctRevisionGeometry(){
        assertEquals(30,Wire.VERSION);assertEquals(66,Wire.ENTITY_FRAME);assertTrue(Wire.knownType(Wire.ENTITY_FRAME));
        assertPair(frame(11,1,false),11,1,false);assertPair(frame(12,2,false),12,2,false);
    }
    @Test void emptyFrameRemovesBaseEffectsAndAvatarsTogether(){assertPair(frame(13,0,true),13,0,true);}
    @Test void mismatchedIdentityAndInvalidEffectStateNeverEncode(){
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,1,7,new byte[31],List.of()));
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(10,2,1,7,base(9,1,0,true),List.of()));
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,2,7,base(9,1,0,true),List.of()));
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,0,1,7,base(9,1,0,true),List.of()));
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,1,0,base(9,1,0,true),List.of()));
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,1,7,base(9,1,0,true),List.of(new ParticleSnapshot.Batch(65,0,new byte[96]))));
    }
    @Test void completeEnvelopeRespectsWireByteBudget(){
        byte[] oversized=new byte[Wire.MAX_PAYLOAD-43];
        ByteBuffer.wrap(oversized).order(ByteOrder.LITTLE_ENDIAN).putLong(9).putLong(1);
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,1,7,oversized,List.of()));
    }

    /** Full envelope oracle independent of both production serializers. */
    private static byte[] expectedFrame(long revision,byte[] base,List<ParticleSnapshot.Batch> effects) {
        int effectLength=36+effects.stream().mapToInt(b->64+b.vertices().length).sum();
        var out=ByteBuffer.allocate(8+base.length+effectLength).order(ByteOrder.LITTLE_ENDIAN);
        out.putInt(base.length).put(base).putInt(effectLength).putLong(9).putLong(2).putLong(revision)
            .putInt(7).putInt(effects.isEmpty()?0:1).putInt(effects.size());
        for(var effect:effects){
            var m=effect.material();out.putInt(effect.texture()).putInt(m.flags()).putInt(m.srcRgb()).putInt(m.dstRgb())
                .putInt(m.srcAlpha()).putInt(m.dstAlpha()).putInt(m.equationRgb()).putInt(m.equationAlpha())
                .putInt(m.depthFunc()).putInt(m.colorMask()).putFloat(m.cutoff()).putFloat(m.red()).putFloat(m.green())
                .putFloat(m.blue()).putFloat(m.alpha()).putInt(effect.vertices().length/24).put(effect.vertices());
        }
        assertEquals(out.capacity(),out.position());return out.array();
    }
    private static byte[] oracleBase(long revision,boolean clear) {
        var out=ByteBuffer.allocate(clear?32:156).order(ByteOrder.LITTLE_ENDIAN);
        out.putLong(9).putLong(revision).putInt(clear?0:1).putInt(0).putInt(clear?0:1);
        if(!clear){
            out.putInt(1).putInt(4).putInt(4);
            for(int i=0;i<4;i++)out.putFloat(i+.25f).putFloat(64).putFloat(-2).putFloat(-0.0f).putFloat(1)
                .putInt(0x89abcdef).putInt(0x00800000+i);
        }
        out.putInt(0);return out.array();
    }
    @Test void synchronousProducerWritesOneCompleteFrameAndCannotRetainDestinationAccess() {
        byte[] base=oracleBase(31,false);var effects=List.of(glint(7));
        byte[] oracle=expectedFrame(31,base,effects);var retained=new AtomicReference<Wire.Writer>();
        var calls=new AtomicInteger();
        var producer=new EntityFrame.Base(base.length,out->{calls.incrementAndGet();retained.set(out);out.bytes(base);});
        byte[] frame=EntityFrame.encode(9,2,31,7,producer,effects);
        assertEquals(1,calls.get());assertArrayEquals(oracle,frame);
        Arrays.fill(base,(byte)0);Arrays.fill(effects.getFirst().vertices(),(byte)0);
        assertThrows(IllegalStateException.class,()->retained.get().u8(99));
        assertThrows(IllegalStateException.class,()->retained.get().bytes(new byte[0]));
        assertThrows(IllegalStateException.class,()->retained.get().toByteArray());
        assertArrayEquals(oracle,frame);
        byte[] clear=oracleBase(32,true);
        assertArrayEquals(expectedFrame(32,clear,List.of()),EntityFrame.encode(9,2,32,7,
            new EntityFrame.Base(clear.length,out->out.bytes(clear)),List.of()));
    }
    @Test void actualWrittenIdentityAndDeclaredProducerLengthAreValidated() {
        byte[] base=oracleBase(41,true);var retained=new AtomicReference<Wire.Writer>();
        for(int delta:new int[]{-1,1}){
            var producer=new EntityFrame.Base(base.length+Math.max(delta,0),out->{
                retained.set(out);out.bytes(base,0,base.length+Math.min(delta,0));
            });
            assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,41,7,producer,List.of()));
            assertThrows(IllegalStateException.class,()->retained.get().u8(0));
        }
        var overrun=new EntityFrame.Base(base.length,out->{retained.set(out);out.bytes(base).u8(1);});
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,41,7,overrun,List.of()));
        assertThrows(IllegalStateException.class,()->retained.get().u8(0));
        for(long[] identity:new long[][]{{10,41},{9,42}}){
            byte[] wrong=base.clone();ByteBuffer.wrap(wrong).order(ByteOrder.LITTLE_ENDIAN).putLong(identity[0]).putLong(identity[1]);
            var producer=new EntityFrame.Base(wrong.length,out->{retained.set(out);out.bytes(wrong);});
            assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,41,7,producer,List.of()));
            assertThrows(IllegalStateException.class,()->retained.get().u8(0));
        }
        var failure=new IllegalStateException("capture failed");
        var broken=new EntityFrame.Base(base.length,out->{retained.set(out);out.i64(9);throw failure;});
        assertSame(failure,assertThrows(IllegalStateException.class,()->EntityFrame.encode(9,2,41,7,broken,List.of())));
        assertThrows(IllegalStateException.class,()->retained.get().u8(0));
        var error=new AssertionError("capture aborted");
        var aborted=new EntityFrame.Base(base.length,out->{retained.set(out);out.i64(9);throw error;});
        assertSame(error,assertThrows(AssertionError.class,()->EntityFrame.encode(9,2,41,7,aborted,List.of())));
        assertThrows(IllegalStateException.class,()->retained.get().u8(0));
    }
    @Test void payloadAndInvalidEffectBudgetsAreCheckedBeforeCallingProducer() {
        var calls=new AtomicInteger();
        assertThrows(IllegalArgumentException.class,()->new EntityFrame.Base(31,out->calls.incrementAndGet()));
        for(int size:new int[]{Wire.MAX_PAYLOAD-43,Integer.MAX_VALUE}){
            var tooLarge=new EntityFrame.Base(size,out->calls.incrementAndGet());
            assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,1,7,tooLarge,List.of()));
        }
        var untouched=new EntityFrame.Base(32,out->calls.incrementAndGet());
        assertThrows(IllegalArgumentException.class,()->EntityFrame.encode(9,2,1,7,untouched,
            List.of(new ParticleSnapshot.Batch(65,0,new byte[96]))));
        assertEquals(0,calls.get());
        // This checks the envelope boundary only; padded bytes are not a valid base mesh for the native parser.
        int baseLength=Wire.MAX_PAYLOAD-44;byte[] zeros=new byte[4096];
        var exact=new EntityFrame.Base(baseLength,out->{
            calls.incrementAndGet();out.i64(9).i64(1);
            for(int left=baseLength-16;left>0;left-=Math.min(left,zeros.length))out.bytes(zeros,0,Math.min(left,zeros.length));
        });
        byte[] frame=EntityFrame.encode(9,2,1,7,exact,List.of());
        assertEquals(Wire.MAX_PAYLOAD,frame.length);assertEquals(1,calls.get());
        var bytes=ByteBuffer.wrap(frame).order(ByteOrder.LITTLE_ENDIAN);assertEquals(baseLength,bytes.getInt());
        bytes.position(4+baseLength);assertEquals(36,bytes.getInt());assertEquals(9,bytes.getLong());
        assertEquals(2,bytes.getLong());assertEquals(1,bytes.getLong());assertEquals(7,bytes.getInt());
        assertEquals(0,bytes.getInt());assertEquals(0,bytes.getInt());assertFalse(bytes.hasRemaining());
    }
}
