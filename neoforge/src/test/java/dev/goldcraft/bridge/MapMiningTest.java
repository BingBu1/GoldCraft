package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.util.Arrays;
import static org.junit.jupiter.api.Assertions.*;

class MapMiningTest {
    @Test void nativeMaterialSamplesAreBoundedAndSeparateFromDamage() {
        var w=new Wire.Writer().i64(11).i64(2).i64(3).i32(1).i32(4).i32(5).i32(72).i32(9).i32(0).f32(20).f32(20)
            .i32(1).i32(MapMining.GLASS).f32(10).f32(-20).f32(30).f32(0).f32(0).f32(1);
        byte[] bytes=w.toByteArray();assertEquals(88,bytes.length);
        var sample=MapMining.surface(bytes);assertEquals(MapMining.GLASS,sample.material());assertEquals(-20,sample.y());
        assertEquals(sample.result().before(),sample.result().after());
        for(int n=0;n<bytes.length;n++) {
            byte[] truncated=Arrays.copyOf(bytes,n);assertThrows(IllegalArgumentException.class,()->MapMining.surface(truncated));
        }
        for(int offset:new int[]{56,60}) {
            byte[] bad=bytes.clone();bad[offset]=(byte)255;assertThrows(IllegalArgumentException.class,()->MapMining.surface(bad));
        }
        Arrays.fill(bytes,84,88,(byte)0);assertThrows(IllegalArgumentException.class,()->MapMining.surface(bytes));
    }
    @Test void disabledUntilAuthoritativePolicyAndResetOnMapChange(){
        var host=new HostWorldState();assertFalse(host.mining().enabled());
        host.world(new Wire.Writer().i64(11).string("cs_assault").toByteArray());
        host.mining(new MapMining.Policy(11,1,2,1).encode());
        assertTrue(host.mining().canRequest(2));assertFalse(host.mining().canRequest(0));
        host.mining(new MapMining.Policy(11,2,0,1).encode());assertFalse(host.mining().enabled());
        host.mining(new MapMining.Policy(11,1,2,3).encode());assertFalse(host.mining().enabled());
        host.mining(new MapMining.Policy(12,10,2,3).encode());assertFalse(host.mining().enabled());
        host.world(new Wire.Writer().i64(12).string("cs_assault").toByteArray());
        assertEquals(MapMining.Policy.none(),host.mining());
        host.mining(new MapMining.Policy(11,20,2,3).encode());assertFalse(host.mining().enabled());
    }
    @Test void unsignedRevisionAndReconnectSnapshot(){
        var host=new HostWorldState();host.world(new Wire.Writer().i64(12).string("cs_assault").toByteArray());
        host.mining(new MapMining.Policy(12,Long.MAX_VALUE,1,1).encode());
        host.mining(new MapMining.Policy(12,Long.MIN_VALUE,0,1).encode());assertFalse(host.mining().enabled());
        host.mining(new MapMining.Policy(12,1,2,3).encode());assertFalse(host.mining().enabled());
        host.clear();host.world(new Wire.Writer().i64(12).string("cs_assault").toByteArray());
        host.mining(new MapMining.Policy(12,Long.MIN_VALUE,0,1).encode());assertEquals(Long.MIN_VALUE,host.mining().revision());
    }
    @Test void malformedPolicyCannotReplaceCurrent(){
        byte[] valid=new MapMining.Policy(7,3,1,1).encode();
        for(int i=0;i<valid.length;i++){
            byte[] truncated=Arrays.copyOf(valid,i);assertThrows(IllegalArgumentException.class,()->MapMining.policy(truncated));
        }
        for(var policy:new MapMining.Policy[]{new MapMining.Policy(0,1,1,1),new MapMining.Policy(1,0,1,1),new MapMining.Policy(1,1,3,1),new MapMining.Policy(1,1,1,4)})
            assertThrows(IllegalArgumentException.class,()->MapMining.policy(policy.encode()));
        assertEquals(1,MapMining.policy(valid).mode());
    }
    @Test void resultPreservesLethalNativeFalseAndRejectsUnknownOutcome(){
        var w=new Wire.Writer().i64(11).i64(2).i64(3).i32(1).i32(4).i32(5).i32(72).i32(9).i32(0).f32(20).f32(-10);
        var result=MapMining.result(w.toByteArray());assertEquals(0,result.status());assertEquals(-10,result.after());
        byte[] invalid=w.toByteArray();invalid[44]=10;assertThrows(IllegalArgumentException.class,()->MapMining.result(invalid));
    }
}
