package dev.goldcraft.world;

import org.junit.jupiter.api.Test;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import static org.junit.jupiter.api.Assertions.*;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class BspMapTest {
    private static BspMap.Vec v(float x,float y,float z){return new BspMap.Vec(x,y,z);}
    private static byte[] floor() {
        ByteBuffer b=ByteBuffer.allocate(372).order(ByteOrder.LITTLE_ENDIAN);b.putInt(30);
        int[] offsets=new int[15],lengths=new int[15];
        offsets[1]=124;lengths[1]=80;offsets[5]=204;lengths[5]=24;
        offsets[9]=228;lengths[9]=24;offsets[10]=252;lengths[10]=56;offsets[14]=308;lengths[14]=64;
        for(int i=0;i<15;i++)b.putInt(offsets[i]).putInt(lengths[i]);
        for(float height:new float[]{0,36,32,18})b.putFloat(0).putFloat(0).putFloat(1).putFloat(height).putInt(2);
        b.putInt(0).putShort((short)-2).putShort((short)-1);b.position(228);
        for(int i=1;i<4;i++)b.putInt(i).putShort((short)-1).putShort((short)-2);
        b.putInt(252,-2);b.putInt(280,-1);b.position(308);
        for(int i=0;i<3;i++)b.putFloat(-128);for(int i=0;i<3;i++)b.putFloat(128);
        for(int i=0;i<3;i++)b.putFloat(0);
        b.putInt(0).putInt(0).putInt(1).putInt(2);return b.array();
    }
    @Test void tracesPreserveHullDimensionsAndSolidFlags() {
        BspMap bsp=BspMap.read(floor());
        float[] heights={0,36,32,18};
        for(int hull=0;hull<4;hull++) {
            var hit=bsp.trace(0,hull,v(0,0,100),v(0,0,-100));
            assertEquals(heights[hull]+0.03125f,hit.end().z(),0.0001);
            assertEquals((100-heights[hull]-0.03125)/200,hit.fraction(),0.00001);
            assertEquals(v(0,0,1),hit.normal());assertFalse(hit.startSolid());assertFalse(hit.allSolid());
        }
        var buried=bsp.trace(0,0,v(0,0,-10),v(0,0,-20));assertTrue(buried.startSolid());assertTrue(buried.allSolid());
        var escape=bsp.trace(0,0,v(0,0,-10),v(0,0,20));assertTrue(escape.startSolid());assertFalse(escape.allSolid());assertEquals(1,escape.fraction());
        assertEquals(BspMap.CLEAR,bsp.classify(0,new BspMap.Bounds(v(-1,-1,0),v(1,1,2))));
        assertEquals(BspMap.FILLED,bsp.classify(0,new BspMap.Bounds(v(-1,-1,-2),v(1,1,0))));
        assertEquals(BspMap.MIXED,bsp.classify(0,new BspMap.Bounds(v(-1,-1,-2),v(1,1,2))));
    }
    @Test void rejectsInvalidGraphRangesAndNonFinitePlanes() {
        byte[] original=floor();
        assertThrows(IllegalArgumentException.class,()->BspMap.read(new byte[123]));
        for(int field:new int[]{12,16,204,208,124}) {
            byte[] bad=original.clone();ByteBuffer b=ByteBuffer.wrap(bad).order(ByteOrder.LITTLE_ENDIAN);
            if(field==208)b.putShort(field,(short)0); // cycle
            else b.putInt(field,field==124?0x7fc00000:Integer.MAX_VALUE);
            assertThrows(IllegalArgumentException.class,()->BspMap.read(bad),"field "+field);
        }
    }
    @Test void readsActualIsolatedMaps() throws Exception {
        Path maps=Path.of("../sandbox/cs-client-a/Half-Life/cstrike/maps");assumeTrue(Files.isDirectory(maps));
        for(String name:new String[]{"de_dust2","cs_militia","de_cbble","cs_assault"}) {
            byte[] bytes=Files.readAllBytes(maps.resolve(name+".bsp"));BspMap bsp=BspMap.read(bytes);
            assertTrue(bsp.planeCount()>100);assertTrue(bsp.nodeCount()>100);assertTrue(bsp.modelCount()>1);
            assertEquals(BspMap.checksum(bytes),bsp.crc());
        }
    }
    @Test void assaultPlayerClipStopsStandingAndCrouchingDespiteClearRenderVolume() throws Exception {
        Path map=Path.of("../sandbox/cs-client-a/Half-Life/cstrike/maps/cs_assault.bsp");assumeTrue(Files.exists(map));
        BspMap bsp=BspMap.read(Files.readAllBytes(map));assertEquals(0xf6725c06L,bsp.crc());
        assertEquals(BspMap.CLEAR,bsp.classify(0,new BspMap.Bounds(v(-1647.9f,-527.9f,0.1f),v(-1616.1f,-336.1f,71.9f))));
        for(int hull:new int[]{1,3}) {
            float center=(hull==1?36:18)+0.03125f;
            var start=v(-1632,-352,center);var motion=v(0,-160,0);
            var stop=HullSlide.move(start,motion,(a,b)->bsp.trace(0,hull,a,b));
            assertEquals(-431.96875,start.add(stop.delta()).y(),0.0001);
            assertTrue(stop.blocked());assertFalse(stop.stuck());
            assertEquals(BspMap.EMPTY,bsp.contents(0,hull,start.add(stop.delta())));
        }
    }
}
