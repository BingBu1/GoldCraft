package dev.goldcraft.client;

import dev.goldcraft.bridge.ParticleSnapshot;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.joml.Matrix4f;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class EffectGeometryTest {
    private static final EffectGeometry.Layout POSITION=new EffectGeometry.Layout(12,0,-1,-1,-1);
    private static final EffectGeometry.Layout PADDED=new EffectGeometry.Layout(40,4,20,24,34);
    private static ByteBuffer vertices(int count,EffectGeometry.Layout layout,int prefix) {
        var bytes=ByteBuffer.allocate(prefix+count*layout.stride()+9).order(ByteOrder.nativeOrder());
        for(int i=0;i<bytes.capacity();i++)bytes.put(i,(byte)0x5a);
        for(int i=0;i<count;i++) {
            int start=prefix+i*layout.stride();
            bytes.putFloat(start+layout.position(),i+1).putFloat(start+layout.position()+4,i+2).putFloat(start+layout.position()+8,i+3);
            if(layout.color()>=0) {
                bytes.put(start+layout.color(),(byte)(128+i)).put(start+layout.color()+1,(byte)(32+i));
                bytes.put(start+layout.color()+2,(byte)(240-i)).put(start+layout.color()+3,(byte)(64+i));
            }
            if(layout.uv()>=0)bytes.putFloat(start+layout.uv(),i*.25f).putFloat(start+layout.uv()+4,-i*.5f);
            if(layout.light()>=0)bytes.putShort(start+layout.light(),(short)0).putShort(start+layout.light()+2,(short)240);
        }
        return bytes.position(prefix).limit(prefix+count*layout.stride());
    }
    private static byte[] convert(ByteBuffer source,int count,EffectGeometry.Layout layout,EffectGeometry.Mode mode) {
        return EffectGeometry.convert(source,count,layout,mode,null,0,new Matrix4f(),0,0,0,false,ParticleSnapshot.MAX_VERTICES);
    }
    private static ByteBuffer decoded(byte[] bytes){return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);}
    private static byte[] remaining(ByteBuffer source){var copy=source.duplicate();byte[] data=new byte[copy.remaining()];copy.get(data);return data;}
    private static void assertOrder(byte[] mesh,int... indices) {
        assertEquals(indices.length*24,mesh.length);var data=decoded(mesh);
        for(int i=0;i<indices.length;i++)assertEquals(indices[i]+1f,data.getFloat(i*24),"output vertex "+i);
    }
    private static ByteBuffer indices(int size,int... values) {
        var bytes=ByteBuffer.allocate(5+values.length*size+7).order(ByteOrder.nativeOrder());
        bytes.position(5);
        for(int value:values){if(size==2)bytes.putShort((short)value);else bytes.putInt(value);}
        bytes.limit(bytes.position()).position(5);return bytes;
    }

    @Test void paddedReadOnlyBufferKeepsPositionOrderAndAllAttributes() {
        var source=vertices(4,PADDED,7).asReadOnlyBuffer().order(ByteOrder.BIG_ENDIAN);
        byte[] original=remaining(source);int position=source.position(),limit=source.limit();
        var matrix=new Matrix4f().translation(10,20,30).scale(2,3,4);
        byte[] mesh=EffectGeometry.convert(source,4,PADDED,EffectGeometry.Mode.QUADS,null,0,matrix,0,0,0,false,4);
        var result=decoded(mesh);
        for(int i=0;i<4;i++) {
            int at=i*24;
            assertEquals(10+2*(i+1),result.getFloat(at));assertEquals(20+3*(i+2),result.getFloat(at+4));
            assertEquals(30+4*(i+3),result.getFloat(at+8));assertEquals(i*.25f,result.getFloat(at+12));
            assertEquals(-i*.5f,result.getFloat(at+16));
            assertEquals(128+i,Byte.toUnsignedInt(result.get(at+20)));assertEquals(32+i,Byte.toUnsignedInt(result.get(at+21)));
            assertEquals(240-i,Byte.toUnsignedInt(result.get(at+22)));assertEquals(64+i,Byte.toUnsignedInt(result.get(at+23)));
        }
        assertEquals(position,source.position());assertEquals(limit,source.limit());assertEquals(ByteOrder.BIG_ENDIAN,source.order());
        assertArrayEquals(original,remaining(source));
    }

    @Test void entityWorldCoordinatesUseZeroOriginAndParticleCameraOriginIsAddedOnceAfterModelView() {
        var source=vertices(4,POSITION,0);
        byte[] world=convert(source,4,POSITION,EffectGeometry.Mode.QUADS);
        assertEquals(1,decoded(world).getFloat(0));assertEquals(2,decoded(world).getFloat(4));assertEquals(3,decoded(world).getFloat(8));
        byte[] particle=EffectGeometry.convert(source,4,POSITION,EffectGeometry.Mode.QUADS,null,0,
            new Matrix4f().translation(4,-5,6).scale(2),100,200,-300,false,4);
        assertEquals(106,decoded(particle).getFloat(0));assertEquals(199,decoded(particle).getFloat(4));
        assertEquals(-288,decoded(particle).getFloat(8));
    }

    @Test void sortedQuadShortAndIntIndicesPreserveOrderAndInputBuffers() {
        for(int size:new int[]{2,4}) {
            var source=vertices(8,POSITION,3);
            var sorted=indices(size,4,5,6,6,7,4,0,1,2,2,3,0).asReadOnlyBuffer().order(ByteOrder.BIG_ENDIAN);
            byte[] original=remaining(sorted);int position=sorted.position(),limit=sorted.limit();
            byte[] mesh=EffectGeometry.convert(source,8,POSITION,EffectGeometry.Mode.QUADS,sorted,size,new Matrix4f(),0,0,0,false,8);
            assertOrder(mesh,4,5,6,7,0,1,2,3);
            assertEquals(position,sorted.position());assertEquals(limit,sorted.limit());assertEquals(ByteOrder.BIG_ENDIAN,sorted.order());
            assertArrayEquals(original,remaining(sorted));assertEquals(3,source.position());
        }
    }

    @Test void shortIndicesAboveSignedShortRangeRemainUnsigned() {
        int count=32772;var sorted=ByteBuffer.allocate(count/4*12).order(ByteOrder.nativeOrder());
        for(int quad=count/4-1;quad>=0;quad--) {
            int a=quad*4;for(int i:new int[]{a,a+1,a+2,a+2,a+3,a})sorted.putShort((short)i);
        }
        sorted.flip();byte[] mesh=EffectGeometry.convert(vertices(count,POSITION,0),count,POSITION,EffectGeometry.Mode.QUADS,
            sorted,2,new Matrix4f(),0,0,0,false,count);
        assertEquals(32769,decoded(mesh).getFloat(0));assertEquals(4,decoded(mesh).getFloat(mesh.length-24));
    }

    @Test void triangleListUsesADegenerateSecondTriangleWithoutChangingWinding() {
        assertOrder(convert(vertices(6,POSITION,0),6,POSITION,EffectGeometry.Mode.TRIANGLES),0,1,2,2,3,4,5,5);
    }

    @Test void stripAlternatesTheFirstTwoVerticesToPreserveWinding() {
        assertOrder(convert(vertices(5,POSITION,0),5,POSITION,EffectGeometry.Mode.TRIANGLE_STRIP),0,1,2,2,2,1,3,3,2,3,4,4);
    }

    @Test void fanKeepsTheSharedCenter() {
        assertOrder(convert(vertices(5,POSITION,0),5,POSITION,EffectGeometry.Mode.TRIANGLE_FAN),0,1,2,2,0,2,3,3,0,3,4,4);
    }

    @Test void absentAttributesDefaultToWhiteAndZeroUv() {
        var mesh=decoded(convert(vertices(4,POSITION,0),4,POSITION,EffectGeometry.Mode.QUADS));
        for(int i=0;i<4;i++) {
            assertEquals(0,mesh.getFloat(i*24+12));assertEquals(0,mesh.getFloat(i*24+16));
            assertEquals(-1,mesh.getInt(i*24+20));
        }
    }

    @Test void onlyLitPolicyBakesUv2AndNeverChangesAlpha() {
        var source=vertices(4,PADDED,0);int[] lights={0,240,120,65535};
        for(int i=0;i<4;i++) {
            int at=i*40;for(int channel=0;channel<3;channel++)source.put(at+20+channel,(byte)200);
            source.put(at+23,(byte)37);source.putShort(at+34,(short)lights[i]);source.putShort(at+36,(short)0);
        }
        var unlit=decoded(EffectGeometry.convert(source,4,PADDED,EffectGeometry.Mode.QUADS,null,0,new Matrix4f(),0,0,0,false,4));
        var lit=decoded(EffectGeometry.convert(source,4,PADDED,EffectGeometry.Mode.QUADS,null,0,new Matrix4f(),0,0,0,true,4));
        int[] brightness={110,200,155,200};
        for(int i=0;i<4;i++) {
            for(int channel=0;channel<3;channel++) {
                assertEquals(200,Byte.toUnsignedInt(unlit.get(i*24+20+channel)));
                assertEquals(brightness[i],Byte.toUnsignedInt(lit.get(i*24+20+channel)));
            }
            assertEquals(37,Byte.toUnsignedInt(lit.get(i*24+23)));
        }
    }

    @Test void malformedLayoutsAndShortSourceAreRejected() {
        for(var layout:new EffectGeometry.Layout[]{new EffectGeometry.Layout(11,0,-1,-1,-1),new EffectGeometry.Layout(12,-1,-1,-1,-1),
            new EffectGeometry.Layout(12,1,-1,-1,-1),new EffectGeometry.Layout(12,0,-2,-1,-1),
            new EffectGeometry.Layout(12,0,9,-1,-1),new EffectGeometry.Layout(12,0,-1,5,-1),new EffectGeometry.Layout(12,0,-1,-1,9)})
            assertThrows(IllegalArgumentException.class,()->convert(ByteBuffer.allocate(160),4,layout,EffectGeometry.Mode.QUADS));
        assertThrows(IllegalArgumentException.class,()->convert(ByteBuffer.allocate(47),4,POSITION,EffectGeometry.Mode.QUADS));
    }

    @Test void incompletePrimitivesAndExpansionBudgetsAreRejected() {
        assertThrows(IllegalArgumentException.class,()->convert(vertices(3,POSITION,0),3,POSITION,EffectGeometry.Mode.QUADS));
        assertThrows(IllegalArgumentException.class,()->convert(vertices(4,POSITION,0),4,POSITION,EffectGeometry.Mode.TRIANGLES));
        for(var mode:new EffectGeometry.Mode[]{EffectGeometry.Mode.TRIANGLES,EffectGeometry.Mode.TRIANGLE_STRIP,EffectGeometry.Mode.TRIANGLE_FAN})
            assertThrows(IllegalArgumentException.class,()->EffectGeometry.convert(vertices(6,POSITION,0),6,POSITION,mode,null,0,new Matrix4f(),0,0,0,false,7));
        assertThrows(IllegalArgumentException.class,()->convert(ByteBuffer.allocate(0),ParticleSnapshot.MAX_VERTICES+1,POSITION,EffectGeometry.Mode.QUADS));
        assertThrows(IllegalArgumentException.class,()->convert(ByteBuffer.allocate(0),-1,POSITION,EffectGeometry.Mode.QUADS));
        assertEquals(0,convert(ByteBuffer.allocate(0),0,POSITION,EffectGeometry.Mode.QUADS).length);
        assertEquals(0,convert(vertices(2,POSITION,0),2,POSITION,EffectGeometry.Mode.TRIANGLE_STRIP).length);
    }

    @Test void malformedSortedIndicesRejectWrongSizeTopologyAndRange() {
        var source=vertices(4,POSITION,0);
        for(int[] values:new int[][]{{0,1,2,0,3,0},{0,1,2,2,3,1},{0,1,2,2,4,0},{-1,1,2,2,3,-1}}) {
            var sorted=indices(4,values);
            assertThrows(IllegalArgumentException.class,()->EffectGeometry.convert(source,4,POSITION,EffectGeometry.Mode.QUADS,sorted,4,new Matrix4f(),0,0,0,false,4));
        }
        assertThrows(IllegalArgumentException.class,()->EffectGeometry.convert(source,4,POSITION,EffectGeometry.Mode.QUADS,ByteBuffer.allocate(11),2,new Matrix4f(),0,0,0,false,4));
        assertThrows(IllegalArgumentException.class,()->EffectGeometry.convert(source,4,POSITION,EffectGeometry.Mode.QUADS,ByteBuffer.allocate(6),1,new Matrix4f(),0,0,0,false,4));
        assertThrows(IllegalArgumentException.class,()->EffectGeometry.convert(vertices(3,POSITION,0),3,POSITION,EffectGeometry.Mode.TRIANGLES,indices(2,0,1,2),2,new Matrix4f(),0,0,0,false,4));
    }

    @Test void nonfiniteCoordinatesUvsAndTransformedBoundsAreRejected() {
        for(int offset:new int[]{4,8,12,24,28})for(float invalid:new float[]{Float.NaN,Float.POSITIVE_INFINITY}) {
            var source=vertices(4,PADDED,0);source.putFloat(offset,invalid);
            assertThrows(IllegalArgumentException.class,()->convert(source,4,PADDED,EffectGeometry.Mode.QUADS));
        }
        var excessiveUv=vertices(4,PADDED,0);excessiveUv.putFloat(24,16.01f);
        assertThrows(IllegalArgumentException.class,()->convert(excessiveUv,4,PADDED,EffectGeometry.Mode.QUADS));
        for(double x:new double[]{Double.NaN,Double.POSITIVE_INFINITY,32_000_001})
            assertThrows(IllegalArgumentException.class,()->EffectGeometry.convert(vertices(4,POSITION,0),4,POSITION,EffectGeometry.Mode.QUADS,null,0,new Matrix4f(),x,0,0,false,4));
        assertThrows(IllegalArgumentException.class,()->EffectGeometry.convert(vertices(4,POSITION,0),4,POSITION,EffectGeometry.Mode.QUADS,null,0,new Matrix4f().scaling(Float.NaN),0,0,0,false,4));
    }
}
