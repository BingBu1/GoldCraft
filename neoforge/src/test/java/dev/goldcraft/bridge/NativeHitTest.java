package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import static org.junit.jupiter.api.Assertions.*;

class NativeHitTest {
    private static byte[] packet(){return new Wire.Writer().i64(41).i64(9).i64(100)
        .i32(2).i32(3).i32(7).i32(11).i32(1).i32(4).i32(8).i32(2)
        .f32(25).f32(32).f32(64).f32(96).toByteArray();}
    @Test void exactPayload(){
        var bytes=packet();assertEquals(72,bytes.length);var hit=NativeHit.read(bytes);
        assertEquals(41,hit.epoch());assertEquals(9,hit.event());assertEquals(100,hit.tick());
        assertEquals(2,hit.slot());assertEquals(3,hit.serial());assertEquals(7,hit.spawn());assertEquals(11,hit.life());
        assertEquals(1,hit.attacker());assertEquals(4,hit.attackerSerial());assertEquals(8,hit.attackerSpawn());
        assertEquals(2,hit.kind());assertEquals(25,hit.amount());assertEquals(96,hit.z());
    }
    @Test void malformedPackets(){
        var original=packet();
        for(int size=0;size<72;size++){var bytes=Arrays.copyOf(original,size);assertThrows(IllegalArgumentException.class,()->NativeHit.read(bytes));}
        assertThrows(IllegalArgumentException.class,()->NativeHit.read(Arrays.copyOf(original,73)));
        for(int offset:new int[]{0,8,16,24,28,32,36}){
            var bytes=original.clone();ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(offset,0);
            assertThrows(IllegalArgumentException.class,()->NativeHit.read(bytes));
        }
        for(int offset:new int[]{56,60,64,68})for(float value:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY}){
            var bytes=original.clone();ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putFloat(offset,value);
            assertThrows(IllegalArgumentException.class,()->NativeHit.read(bytes));
        }
        for(int[] mutation:new int[][]{{24,65},{40,0},{44,0},{48,0},{52,8}}){
            var bytes=original.clone();ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(mutation[0],mutation[1]);
            assertThrows(IllegalArgumentException.class,()->NativeHit.read(bytes));
        }
    }
}
