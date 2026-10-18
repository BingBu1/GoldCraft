package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.util.Arrays;
import java.util.UUID;
import static org.junit.jupiter.api.Assertions.*;

class NativeViewTest {
    private static HostWorldState host(){
        var h=new HostWorldState();h.world(new Wire.Writer().i64(41).string("cs_assault").toByteArray());actors(h,1,3,4,5);return h;
    }
    private static void actors(HostWorldState h,long tick,int serial,int spawn,int life){
        var w=new Wire.Writer().i64(h.epoch()).i64(tick).f32(1).i32(0).i32(1)
            .i32(1).i32(serial).i32(1).i32(1).i32(1).i32(life).f32(100).f32(0);
        for(int i=0;i<14;i++)w.f32(0);
        h.actors(w.bytes(Wire.uuid(new UUID(0,0))).i32(spawn).i64(0).toByteArray());
    }
    private static NativeView.View view(int flags){return new NativeView.View(1,3,4,5,10,20,53,flags);}
    private static byte[] packet(long epoch,long tick,NativeView.View... values){
        var w=new Wire.Writer().i64(epoch).i64(tick).i32(values.length);
        for(var v:values)w.i32(v.slot()).i32(v.serial()).i32(v.spawn()).i32(v.life()).f32(v.x()).f32(v.y()).f32(v.z()).i32(v.flags());
        return w.toByteArray();
    }
    @Test void actualEyeAndLeaseRejectReplays(){
        var h=host();var v=h.nativeView();var a=h.actors().getFirst();
        assertTrue(v.accept(h,packet(41,1,view(1)),10));assertEquals(view(1),v.get(h,a,16));
        assertTrue(v.get(h,a,10).water());assertEquals(53,v.get(h,a,10).z());
        assertFalse(v.accept(h,packet(41,1,view(0)),16));assertNull(v.get(h,a,17));assertNull(v.get(h,a,9));
        actors(h,2,3,4,5);assertNull(v.get(h,a,10));assertTrue(v.accept(h,packet(41,2,view(0)),20));assertFalse(v.get(h,a,20).water());
        actors(h,3,3,4,5);assertTrue(v.accept(h,packet(41,3),21));assertNull(v.get(h,a,21));
    }
    @Test void snapshotsWorldsAndReusedIdentitiesNeverMix(){
        var h=host();var v=h.nativeView();assertFalse(v.accept(h,packet(42,1,view(1)),0));assertFalse(v.accept(h,packet(41,2,view(1)),0));
        assertTrue(v.accept(h,packet(41,1,view(1)),0));
        for(int[] ids:new int[][]{{9,4,5},{3,9,5},{3,4,9}}){
            actors(h,h.tick()+1,ids[0],ids[1],ids[2]);assertNull(v.get(h,h.actors().getFirst(),0));
            assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,h.tick(),view(1)),0));
        }
        h.clear();assertNull(v.get(h,null,0));
    }
    @Test void malformedBatchesAreAtomic(){
        var h=host();var v=h.nativeView();var good=packet(41,1,view(1));
        for(int n=0;n<good.length;n++){final var b=Arrays.copyOf(good,n);assertThrows(IllegalArgumentException.class,()->v.accept(h,b,0));}
        assertThrows(IllegalArgumentException.class,()->v.accept(h,Arrays.copyOf(good,good.length+1),0));
        assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,1,view(1),view(1)),0));
        for(float bad:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY})
            assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,1,new NativeView.View(1,3,4,5,bad,0,0,1)),0));
        for(int flags:new int[]{2,-1,8})assertThrows(IllegalArgumentException.class,()->v.accept(h,packet(41,1,view(flags)),0));
        assertTrue(v.accept(h,good,0));assertEquals(view(1),v.get(h,h.actors().getFirst(),0));
    }
}
