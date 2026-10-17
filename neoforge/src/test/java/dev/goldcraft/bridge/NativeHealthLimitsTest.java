package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.util.Arrays;
import java.util.UUID;
import static org.junit.jupiter.api.Assertions.*;

class NativeHealthLimitsTest {
    private static HostWorldState host(){
        var host=new HostWorldState();host.world(new Wire.Writer().i64(41).string("cs_assault").toByteArray());
        actors(host,1,3,4,5);return host;
    }
    private static void actors(HostWorldState host,long tick,int serial,int spawn,int life){
        var w=new Wire.Writer().i64(host.epoch()).i64(tick).f32(1).i32(0).i32(1)
            .i32(1).i32(serial).i32(1).i32(1).i32(1).i32(life).f32(12000).f32(0);
        for(int i=0;i<14;i++)w.f32(0);
        host.actors(w.bytes(Wire.uuid(new UUID(0,0))).i32(spawn).i64(0).toByteArray());
    }
    private static byte[] packet(long epoch,long tick,NativeHealthLimits.Limit... values){
        var w=new Wire.Writer().i64(epoch).i64(tick).i32(values.length);
        for(var v:values)w.i32(v.slot()).i32(v.serial()).i32(v.spawn()).i32(v.life()).f32(v.base()).f32(v.effective()).i64(v.revision());
        return w.toByteArray();
    }
    private static NativeHealthLimits.Limit limit(float base,float effective){return new NativeHealthLimits.Limit(1,3,4,5,base,effective,9);}
    @Test void highClassBaseAndModifiedLimitAreIndependent(){
        var h=host();var limits=h.healthLimits();var value=limit(12000,12040);
        assertTrue(limits.accept(h,packet(41,1,value)));
        assertEquals(value,limits.get(h,h.actors().getFirst()));
        assertFalse(limits.accept(h,packet(41,1,limit(100,100))));
        var bytes=NativeHealthLimits.encode(41,10,value,12060);assertEquals(40,bytes.length);
        var r=new Wire.Reader(bytes);assertEquals(41,r.i64());assertEquals(10,r.i64());
        assertEquals(1,r.i32());assertEquals(3,r.i32());assertEquals(4,r.i32());assertEquals(5,r.i32());
        assertEquals(12000,r.f32());assertEquals(12060,r.f32());r.finish();
    }
    @Test void splitSnapshotsAndReusedSlotsNeverMix(){
        var h=host();var limits=h.healthLimits();limits.accept(h,packet(41,1,limit(100,120)));
        actors(h,2,3,4,5);assertNull(limits.get(h,h.actors().getFirst()));
        assertFalse(limits.accept(h,packet(41,1,limit(100,120))));
        assertFalse(limits.accept(h,packet(41,3,limit(100,120))));
        assertFalse(limits.accept(h,packet(42,2,limit(100,120))));
        assertTrue(limits.accept(h,packet(41,2,limit(200,240))));
        assertEquals(200,limits.get(h,h.actors().getFirst()).base());
        for(int[] identity:new int[][]{{9,4,5},{3,9,5},{3,4,9}}){
            actors(h,h.tick()+1,identity[0],identity[1],identity[2]);
            assertThrows(IllegalArgumentException.class,()->limits.accept(h,packet(41,h.tick(),limit(200,240))));
            assertNull(limits.get(h,h.actors().getFirst()));
        }
        h.clear();assertNull(limits.get(h,null));
    }
    @Test void invalidBatchDoesNotCommitAnyRecord(){
        var h=host();var limits=h.healthLimits();var good=packet(41,1,limit(12000,12020));
        for(int size=0;size<good.length;size++){
            final var truncated=Arrays.copyOf(good,size);
            assertThrows(IllegalArgumentException.class,()->limits.accept(h,truncated));
        }
        assertThrows(IllegalArgumentException.class,()->limits.accept(h,Arrays.copyOf(good,good.length+1)));
        assertThrows(IllegalArgumentException.class,()->limits.accept(h,packet(41,1,limit(100,120),limit(200,240))));
        for(float invalid:new float[]{0,-1,10_000_001,Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY}){
            assertThrows(IllegalArgumentException.class,()->limits.accept(h,packet(41,1,limit(invalid,120))));
            assertThrows(IllegalArgumentException.class,()->limits.accept(h,packet(41,1,limit(100,invalid))));
            assertThrows(IllegalArgumentException.class,()->NativeHealthLimits.encode(41,1,limit(100,120),invalid));
        }
        assertTrue(limits.accept(h,good));assertEquals(12000,limits.get(h,h.actors().getFirst()).base());
        actors(h,2,3,4,5);assertTrue(limits.accept(h,packet(41,2)));assertNull(limits.get(h,h.actors().getFirst()));
    }
    @Test void allIdentityFieldsAndWireBoundsAreChecked(){
        var h=host();var limits=h.healthLimits();
        for(var value:new NativeHealthLimits.Limit[]{new NativeHealthLimits.Limit(0,3,4,5,100,120,1),new NativeHealthLimits.Limit(65,3,4,5,100,120,1),new NativeHealthLimits.Limit(1,0,4,5,100,120,1),new NativeHealthLimits.Limit(1,3,0,5,100,120,1),new NativeHealthLimits.Limit(1,3,4,0,100,120,1)}){
            assertThrows(IllegalArgumentException.class,()->limits.accept(h,packet(41,1,value)));
            assertThrows(IllegalArgumentException.class,()->NativeHealthLimits.encode(41,1,value,120));
        }
        assertThrows(IllegalArgumentException.class,()->NativeHealthLimits.encode(0,1,limit(100,120),120));
        assertThrows(IllegalArgumentException.class,()->NativeHealthLimits.encode(41,0,limit(100,120),120));
        assertThrows(IllegalArgumentException.class,()->limits.accept(h,new Wire.Writer().i64(41).i64(1).i32(65).toByteArray()));
        for(float bound:new float[]{1,10_000_000})assertEquals(40,NativeHealthLimits.encode(41,1,limit(bound,bound),bound).length);
    }
}
