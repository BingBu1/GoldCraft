package dev.goldcraft.client;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class PostLightmapTest {
    private static final EffectGeometry.Layout PARTICLE=new EffectGeometry.Layout(28,0,20,12,24);
    @Test void usesActualRgbTexelAndPreservesSourceAndLayout(){
        var source=ByteBuffer.allocate(28).order(ByteOrder.nativeOrder());
        source.putFloat(0,7).putFloat(12,.4f).putInt(20,0xffffffff).putShort(24,(short)32).putShort(26,(short)48);
        int[] map=new int[256];map[3*16+2]=0xff204080;
        var shaded=PostLightmap.shade(source,1,PARTICLE,map,false);
        assertEquals(0xff204080,shaded.getInt(20));assertEquals(0xffffffff,source.getInt(20));
        assertEquals(7,shaded.getFloat(0));assertEquals(.4f,shaded.getFloat(12));
    }
    @Test void rejectsInterpolatedNormalizedCoordinatesInsteadOfGuessing(){
        var source=ByteBuffer.allocate(56).order(ByteOrder.nativeOrder());
        source.putShort(24,(short)240).putShort(26,(short)240).putShort(52,(short)0).putShort(54,(short)240);
        assertThrows(IllegalArgumentException.class,()->PostLightmap.shade(source,2,PARTICLE,new int[256],true));
    }
    @Test void constantNormalizedLightmapUsesClampToEdge(){
        var source=ByteBuffer.allocate(28).order(ByteOrder.nativeOrder());
        source.putInt(20,0xffffffff).putShort(24,(short)240).putShort(26,(short)240);
        int[] map=new int[256];Arrays.fill(map,0);map[255]=0xffabcdef;
        assertEquals(0xffabcdef,PostLightmap.shade(source,1,PARTICLE,map,true).getInt(20));
    }
}
