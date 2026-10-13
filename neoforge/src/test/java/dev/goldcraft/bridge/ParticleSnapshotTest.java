package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ParticleSnapshotTest {
    @Test void expirationIsAnExplicitEmptySnapshot(){
        byte[] clear=ParticleSnapshot.encode(123,7,11,2,0,List.of());
        assertEquals(36,clear.length);
        Wire.Reader r=new Wire.Reader(clear);assertEquals(123,r.i64());assertEquals(7,r.i64());assertEquals(11,r.i64());
        assertEquals(2,r.i32());assertEquals(0,r.i32());assertEquals(0,r.i32());r.finish();
    }
    @Test void invalidTopologyAndX86BudgetsFailBeforeTransmission(){
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encode(1,1,1,1,1,List.of(new ParticleSnapshot.Batch(1,0,new byte[24]))));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encode(1,1,1,1,1,List.of(new ParticleSnapshot.Batch(65,0,new byte[96]))));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encode(1,1,1,1,1,List.of(new ParticleSnapshot.Batch(1,2,new byte[96]))));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encode(1,1,1,1,1,List.of(new ParticleSnapshot.Batch(1,0,new byte[(65536+4)*24]))));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encode(1,1,1,0,0,List.of()));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encode(1,1,1,1,1,List.of()));
    }

    private static byte[] vertices(float shift) {
        var out=ByteBuffer.allocate(96).order(ByteOrder.LITTLE_ENDIAN);
        for(int i=0;i<4;i++)out.putFloat(shift+i).putFloat(64-i).putFloat(-2+i)
            .putFloat(i==0?-0.0f:i*.25f).putFloat(1-i).putInt(0x80a0b0c0+i);
        return out.array();
    }
    private static List<ParticleSnapshot.Batch> batches() {
        return List.of(new ParticleSnapshot.Batch(-1,new ParticleSnapshot.Material(6|128|256,
                768,1,0,1,32774,32778,514,15,0,1,.5f,.25f,1),vertices(-10)),
            new ParticleSnapshot.Batch(64,new ParticleSnapshot.Material(1|8|16|64,
                776,775,774,773,32779,32774,519,5,.125f,4,3,2,.75f),vertices(100)));
    }
    /** Explicit protocol layout, independent of the material and snapshot writers. */
    private static byte[] expected(long epoch,long generation,long revision,int life,int count,List<ParticleSnapshot.Batch> batches) {
        int length=36+batches.stream().mapToInt(b->64+b.vertices().length).sum();
        var out=ByteBuffer.allocate(length).order(ByteOrder.LITTLE_ENDIAN);
        out.putLong(epoch).putLong(generation).putLong(revision).putInt(life).putInt(count).putInt(batches.size());
        for(var batch:batches){
            var m=batch.material();
            out.putInt(batch.texture()).putInt(m.flags()).putInt(m.srcRgb()).putInt(m.dstRgb()).putInt(m.srcAlpha())
                .putInt(m.dstAlpha()).putInt(m.equationRgb()).putInt(m.equationAlpha()).putInt(m.depthFunc())
                .putInt(m.colorMask()).putFloat(m.cutoff()).putFloat(m.red()).putFloat(m.green()).putFloat(m.blue())
                .putFloat(m.alpha()).putInt(batch.vertices().length/24).put(batch.vertices());
        }
        assertEquals(length,out.position());return out.array();
    }
    @Test void directRegionAndCompatibilityEncoderMatchAllMaterialBytesAndOwnInput() {
        var batches=batches();byte[] oracle=expected(0x1122334455667788L,3,9,7,2,batches);
        assertEquals(oracle.length,ParticleSnapshot.encodedSize(0x1122334455667788L,3,9,7,2,batches));
        byte[] direct=new byte[oracle.length+11];Arrays.fill(direct,(byte)0x5a);
        ParticleSnapshot.encodeInto(0x1122334455667788L,3,9,7,2,batches,direct,5,oracle.length);
        byte[] compatible=ParticleSnapshot.encode(0x1122334455667788L,3,9,7,2,batches);
        for(var batch:batches)Arrays.fill(batch.vertices(),(byte)0);
        assertArrayEquals(oracle,compatible);assertArrayEquals(oracle,Arrays.copyOfRange(direct,5,direct.length-6));
        for(int i=0;i<5;i++)assertEquals((byte)0x5a,direct[i]);
        for(int i=direct.length-6;i<direct.length;i++)assertEquals((byte)0x5a,direct[i]);
        byte[] clear=new byte[36];ParticleSnapshot.encodeInto(17,19,23,29,0,List.of(),clear,0,clear.length);
        assertArrayEquals(expected(17,19,23,29,0,List.of()),clear);
    }
    @Test void regionLengthIsExactAndValidationDoesNotPartiallyPublish() {
        var batches=batches();int size=ParticleSnapshot.encodedSize(1,2,3,4,2,batches);
        byte[] destination=new byte[size+8],untouched=destination.clone();
        for(int length:new int[]{size-1,size+1,-1,Integer.MAX_VALUE}){
            assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encodeInto(1,2,3,4,2,batches,destination,4,length));
            assertArrayEquals(untouched,destination);
        }
        var malformed=List.of(batches.getFirst(),new ParticleSnapshot.Batch(65,0,new byte[96]));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encodedSize(1,2,3,4,2,malformed));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encodeInto(1,2,3,4,2,malformed,destination,4,size));
        assertArrayEquals(untouched,destination);
    }
    @Test void exactBatchVertexAndParticleLimitsMatchSizingAndDirectWriting() {
        var small=new ParticleSnapshot.Batch(0,0,new byte[96]);
        var maxBatches=Collections.nCopies(128,small);int batchSize=36+128*(64+96);
        assertEquals(batchSize,ParticleSnapshot.encodedSize(1,2,3,4,4096,maxBatches));
        byte[] direct=new byte[batchSize];ParticleSnapshot.encodeInto(1,2,3,4,4096,maxBatches,direct,0,direct.length);
        assertArrayEquals(expected(1,2,3,4,4096,maxBatches),direct);
        var maxVertices=List.of(new ParticleSnapshot.Batch(0,0,new byte[65536*24]));
        assertEquals(36+64+65536*24,ParticleSnapshot.encodedSize(1,2,3,4,1,maxVertices));
        assertEquals(36+64+65536*24,ParticleSnapshot.encode(1,2,3,4,1,maxVertices).length);
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encodedSize(1,2,3,4,1,Collections.nCopies(129,small)));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encodedSize(1,2,3,4,4097,maxBatches));
        assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encodedSize(1,2,3,4,1,
            List.of(maxVertices.getFirst(),small)));
        for(long[] identity:new long[][]{{0,2,3,4},{1,0,3,4},{1,-1,3,4},{1,2,0,4},{1,2,-1,4},{1,2,3,0}})
            assertThrows(IllegalArgumentException.class,()->ParticleSnapshot.encodedSize(identity[0],identity[1],identity[2],(int)identity[3],0,List.of()));
    }
}
