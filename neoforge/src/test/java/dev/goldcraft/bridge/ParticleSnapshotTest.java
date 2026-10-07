package dev.goldcraft.bridge;

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
}
