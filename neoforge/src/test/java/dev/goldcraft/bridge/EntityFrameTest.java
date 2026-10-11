package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.List;
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
        assertEquals(26,Wire.VERSION);assertEquals(66,Wire.ENTITY_FRAME);assertTrue(Wire.knownType(Wire.ENTITY_FRAME));
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
}
