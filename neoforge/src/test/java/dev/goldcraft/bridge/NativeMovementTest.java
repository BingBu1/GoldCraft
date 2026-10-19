package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.util.Arrays;
import java.util.UUID;
import static org.junit.jupiter.api.Assertions.*;

class NativeMovementTest {
    private static HostWorldState host(){
        var h=new HostWorldState();h.world(new Wire.Writer().i64(41).string("cs_assault").toByteArray());actors(h,1,3,4,5,1);return h;
    }
    private static void actors(HostWorldState h,long tick,int serial,int spawn,int life,int flags){
        var w=new Wire.Writer().i64(h.epoch()).i64(tick).f32(1).i32(0).i32(1)
            .i32(1).i32(serial).i32(1).i32(1).i32(flags).i32(life).f32(100).f32(0);
        for(int i=0;i<14;i++)w.f32(0);
        h.actors(w.bytes(Wire.uuid(new UUID(0,0))).i32(spawn).i64(0).toByteArray());
    }
    private static NativeMovement.State state(int flags){return new NativeMovement.State(1,3,4,5,320,-640,128,flags);}
    private static byte[] packet(long epoch,long tick,NativeMovement.State... values){
        var w=new Wire.Writer().i64(epoch).i64(tick).i32(values.length);
        for(var v:values)w.i32(v.slot()).i32(v.serial()).i32(v.spawn()).i32(v.life()).f32(v.x()).f32(v.y()).f32(v.z()).i32(v.flags());
        return w.toByteArray();
    }
    @Test void exactGroundAirAndConvertedVelocity(){
        var h=host();var v=h.nativeMovement();
        for(int flags=0;flags<4;flags++){
            if(flags>0)actors(h,h.tick()+1,3,4,5,1);
            assertTrue(v.accept(h,packet(41,h.tick(),state(flags)),10));
            var s=v.get(h,h.actors().getFirst(),10);
            assertEquals((flags&1)!=0,s.grounded());assertEquals((flags&2)!=0,s.walking());
            assertEquals(.5,s.velocityX());assertEquals(.2,s.velocityY());assertEquals(1,s.velocityZ());
        }
    }
    @Test void staleMissingAndReusedIdentitiesNeverMix(){
        var h=host();var v=h.nativeMovement();var a=h.actors().getFirst();
        assertNull(v.get(h,a,10));assertFalse(v.accept(h,packet(42,1,state(3)),10));
        assertFalse(v.accept(h,packet(41,2,state(3)),10));assertTrue(v.accept(h,packet(41,1,state(3)),10));
        assertFalse(v.accept(h,packet(41,1,state(0)),11));assertEquals(state(3),v.get(h,a,16));
        assertNull(v.get(h,a,17));assertNull(v.get(h,a,9));
        for(int[] ids:new int[][]{{9,4,5,1},{3,9,5,1},{3,4,9,1},{3,4,5,0},{3,4,5,5}}){
            actors(h,h.tick()+1,ids[0],ids[1],ids[2],ids[3]);assertNull(v.get(h,h.actors().getFirst(),10));
            assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,h.tick(),state(3)),10));
        }
        actors(h,h.tick()+1,3,4,5,1);assertTrue(v.accept(h,packet(41,h.tick()),11));assertNull(v.get(h,h.actors().getFirst(),11));
        h.clear();assertNull(v.get(h,a,11));
    }
    @Test void malformedBatchesAreAtomic(){
        var h=host();var v=h.nativeMovement();var good=packet(41,1,state(3));
        for(int n=0;n<good.length;n++){final var b=Arrays.copyOf(good,n);assertThrows(IllegalArgumentException.class,()->v.accept(h,b,0));}
        assertThrows(IllegalArgumentException.class,()->v.accept(h,Arrays.copyOf(good,good.length+1),0));
        assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,1,state(3),state(3)),0));
        for(float bad:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY,65537,-65537})
            assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,1,new NativeMovement.State(1,3,4,5,bad,0,0,3)),0));
        for(int flags:new int[]{4,-1,8})assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,1,state(flags)),0));
        assertTrue(v.accept(h,good,0));assertEquals(state(3),v.get(h,h.actors().getFirst(),0));
    }
}
