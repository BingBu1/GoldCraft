package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import static org.junit.jupiter.api.Assertions.*;

class NativeArmorProtocolTest {
    private static byte[] request(){
        return ByteBuffer.allocate(64).order(ByteOrder.LITTLE_ENDIAN)
            .putLong(0x1020304050607080L).putLong(-1L)
            .putInt(2).putInt(7).putInt(9).putInt(11)
            .putInt(3).putInt(13).putInt(17).putFloat(40).putInt(2)
            .putFloat(320).putFloat(-160).putFloat(36).array();
    }
    @Test void nativeLayoutAndUnsignedEventSurviveWithoutNativePointers(){
        var q=NativeArmorProtocol.read(request());
        assertEquals(-1L,q.event());assertEquals(2,q.slot());assertEquals(11,q.life());
        assertEquals(3,q.attacker());assertEquals(17,q.attackerSpawn());assertEquals(-160,q.y());
        byte[] result=q.result(0,11.2f,.36f);
        var r=ByteBuffer.wrap(result).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(44,result.length);assertEquals(q.world(),r.getLong());assertEquals(q.event(),r.getLong());
        assertEquals(2,r.getInt());assertEquals(7,r.getInt());assertEquals(9,r.getInt());assertEquals(11,r.getInt());
        assertEquals(0,r.getInt());assertEquals(11.2f,r.getFloat());assertEquals(.36f,r.getFloat());
    }
    @Test void truncatedTrailingAndInvalidIdentityCannotReachEquipmentWear(){
        byte[] good=request();
        for(int size=0;size<good.length;size++){
            byte[] truncated=Arrays.copyOf(good,size);
            assertThrows(IllegalArgumentException.class,()->NativeArmorProtocol.read(truncated));
        }
        assertThrows(IllegalArgumentException.class,()->NativeArmorProtocol.read(Arrays.copyOf(good,65)));
        for(int offset:new int[]{16,20,24,28,36,40}){
            byte[] bad=good.clone();ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putInt(offset,0);
            assertThrows(IllegalArgumentException.class,()->NativeArmorProtocol.read(bad));
        }
        for(float value:new float[]{Float.NaN,Float.POSITIVE_INFINITY,-1,0,500001}){
            byte[] bad=good.clone();ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN).putFloat(44,value);
            assertThrows(IllegalArgumentException.class,()->NativeArmorProtocol.read(bad));
        }
        var q=NativeArmorProtocol.read(good);
        assertThrows(IllegalArgumentException.class,()->q.result(0,41,1));
        assertThrows(IllegalArgumentException.class,()->q.result(0,1,Float.NaN));
        assertThrows(IllegalArgumentException.class,()->q.result(0,1,1.01f));
    }
}
