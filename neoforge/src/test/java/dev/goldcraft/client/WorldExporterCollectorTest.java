package dev.goldcraft.client;

import dev.goldcraft.bridge.Wire;
import java.util.Random;
import net.minecraft.client.render.VertexConsumer;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class WorldExporterCollectorTest {
    /** Frozen byte-at-a-time producer, independent of the new packed writer. */
    private static final class LegacyCollector implements VertexConsumer {
        private final Wire.Writer writer=new Wire.Writer();
        private float x,y,z,u,v,offsetX,offsetY,offsetZ;
        private int red=255,green=255,blue=255,alpha=255,count,sourceCount,overlayColor;
        private final boolean withOverlay,triangles;
        private int hurtVertices;
        private boolean pending;
        LegacyCollector(boolean withOverlay,boolean triangles){this.withOverlay=withOverlay;this.triangles=triangles;}
        void offset(float x,float y,float z){offsetX=x;offsetY=y;offsetZ=z;}
        private void flush(){
            if(!pending)return;
            sourceCount++;writeVertex();
            if(triangles&&sourceCount%3==0)writeVertex();
            pending=false;
        }
        private void writeVertex(){
            if(++count>262144)throw new IllegalArgumentException("Section vertex budget exceeded");
            writer.f32(x).f32(y).f32(z).f32(u).f32(v).u8(red).u8(green).u8(blue).u8(alpha);
            if(withOverlay){writer.i32(overlayColor);if(overlayColor==0x4d0000ff)hurtVertices++;}
        }
        byte[] finish(){flush();if(sourceCount%(triangles?3:4)!=0)throw new IllegalArgumentException("Incomplete model primitive");return writer.toByteArray();}
        @Override public VertexConsumer vertex(float x,float y,float z){flush();this.x=x+offsetX;this.y=y+offsetY;this.z=z+offsetZ;overlayColor=0;pending=true;return this;}
        @Override public VertexConsumer color(int red,int green,int blue,int alpha){this.red=red;this.green=green;this.blue=blue;this.alpha=alpha;return this;}
        @Override public VertexConsumer texture(float u,float v){this.u=u;this.v=v;return this;}
        @Override public VertexConsumer overlay(int u,int v){overlayColor=dev.goldcraft.bridge.OverlayColor.fromUv(u,v);return this;}
        @Override public VertexConsumer light(int u,int v){return this;}
        @Override public VertexConsumer normal(float x,float y,float z){return this;}
    }

    private record Pair(WorldExporter.Collector actual,LegacyCollector old) {
        Pair(boolean overlay,boolean triangles){this(new WorldExporter.Collector(overlay,triangles),new LegacyCollector(overlay,triangles));}
        void offset(float x,float y,float z){actual.offset(x,y,z);old.offset(x,y,z);}
        void same(){
            byte[] expected=old.finish();assertArrayEquals(expected,actual.finish());
            assertEquals(expected.length,actual.finishSize());
            var direct=new Wire.Writer(expected.length);actual.writeTo(direct,expected.length);
            assertArrayEquals(expected,direct.toByteArray());assertEquals(old.hurtVertices,actual.hurtVertices());
        }
    }
    private static void populate(VertexConsumer target,int count,long seed){
        Random random=new Random(seed);
        for(int i=0;i<count;i++){
            target.vertex(random.nextFloat()*65536-32768,random.nextFloat()*4096-2048,random.nextFloat()*65536-32768)
                .texture(random.nextFloat()*32-16,random.nextFloat()*32-16)
                .color(random.nextInt(),random.nextInt(),random.nextInt(),random.nextInt());
            if(i%3!=0)target.overlay(i%31-8,i%17-1);
            target.light(i,i*3).normal(Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY);
        }
    }
    private static void constant(VertexConsumer target,int count){for(int i=0;i<count;i++)target.vertex(-123.5f,0,19.25f);}
    private static String failure(Runnable run){return assertThrows(IllegalArgumentException.class,run::run).getMessage();}

    @Test void animatedTriangleAndQuadBytesMatchLegacy(){
        for(boolean overlay:new boolean[]{false,true})for(boolean triangles:new boolean[]{false,true}){
            Pair pair=new Pair(overlay,triangles);pair.offset(-4096.25f,63.125f,8192.5f);
            int count=triangles?3186:8192;
            populate(pair.actual,count,0x4743);populate(pair.old,count,0x4743);pair.same();
            assertEquals((triangles?count/3*4:count)*(overlay?28:24),pair.actual.finish().length);
        }
    }
    @Test void pendingAttributesAndOverlayResetStayIdentical(){
        Pair pair=new Pair(true,false);
        for(VertexConsumer target:new VertexConsumer[]{pair.actual,pair.old}){
            target.vertex(0,0,0).texture(-0.0f,Float.MIN_VALUE).color(-1,256,511,128).overlay(15,0);
            target.vertex(-0.0f,Float.MAX_VALUE,-Float.MAX_VALUE);
            target.color(1,2,3,4).color(200,201,202,203).texture(1,2).texture(3,4);
            target.vertex(1,2,3).overlay(0,15);
            target.vertex(4,5,6).overlay(15,0);
        }
        pair.same();assertEquals(2,pair.actual.hurtVertices());
    }
    @Test void returnedFramesOwnTheirBytesAndFinishDoesNotFlushTwice(){
        Pair pair=new Pair(true,false);constant(pair.actual,4);constant(pair.old,4);pair.same();
        byte[] first=pair.actual.finish(),expected=first.clone();first[0]^=127;
        assertArrayEquals(expected,pair.actual.finish());
        constant(pair.actual,4);constant(pair.old,4);pair.same();
        assertEquals(4*28,first.length);assertEquals(8*28,pair.actual.finish().length);
    }
    @Test void directOutputAcrossCapacityEdgesOwnsBytesWithoutFlushingTwice(){
        for(boolean overlay:new boolean[]{false,true})for(boolean triangles:new boolean[]{false,true})
            for(int primitives:new int[]{0,1,36,37,42,43,73,86}){
                Pair pair=new Pair(overlay,triangles);int count=primitives*(triangles?3:4);
                populate(pair.actual,count,713);populate(pair.old,count,713);pair.same();
                int size=pair.actual.finishSize();var writer=new Wire.Writer(size+1).u8(37);
                pair.actual.writeTo(writer,size);byte[] before=writer.toByteArray();
                constant(pair.actual,triangles?3:4);constant(pair.old,triangles?3:4);pair.same();
                assertArrayEquals(before,writer.toByteArray());
                assertEquals("Changed model vertex count",failure(()->pair.actual.writeTo(writer,size)));
                assertArrayEquals(before,writer.toByteArray());
            }
    }
    @Test void invalidDirectOutputDoesNotTouchDestinationAndFreshCollectorRecovers(){
        var writer=new Wire.Writer(1).u8(91);
        var incomplete=new WorldExporter.Collector(true,false);constant(incomplete,3);
        assertEquals("Incomplete model primitive",failure(()->incomplete.writeTo(writer,3*28)));
        var nonfinite=new WorldExporter.Collector(true,false);nonfinite.vertex(Float.NaN,0,0);
        assertEquals("Non-finite float",failure(()->nonfinite.writeTo(writer,28)));
        assertArrayEquals(new byte[]{91},writer.toByteArray());
        Pair recovered=new Pair(true,false);constant(recovered.actual,4);constant(recovered.old,4);recovered.same();
        recovered.actual.writeTo(writer,recovered.actual.finishSize());
        var expected=new Wire.Writer().u8(91).bytes(recovered.old.finish());
        assertArrayEquals(expected.toByteArray(),writer.toByteArray());
    }
    @Test void nonFinitePositionsUvsAndOverflowRemainRejected(){
        for(boolean overlay:new boolean[]{false,true})for(int field=0;field<5;field++)
            for(float invalid:new float[]{Float.NaN,Float.POSITIVE_INFINITY,Float.NEGATIVE_INFINITY}){
                Pair pair=new Pair(overlay,false);float[] v={1,2,3,4,5};v[field]=invalid;
                for(VertexConsumer target:new VertexConsumer[]{pair.actual,pair.old})target.vertex(v[0],v[1],v[2]).texture(v[3],v[4]);
                assertEquals(failure(pair.old::finish),failure(pair.actual::finish));
            }
        Pair pair=new Pair(true,false);pair.offset(Float.MAX_VALUE,0,0);
        pair.actual.vertex(Float.MAX_VALUE,0,0);pair.old.vertex(Float.MAX_VALUE,0,0);
        assertEquals(failure(pair.old::finish),failure(pair.actual::finish));
    }
    @Test void emptyAndIncompletePrimitiveRulesStayIdentical(){
        for(boolean triangles:new boolean[]{false,true})for(int count=0;count<9;count++){
            Pair pair=new Pair(false,triangles);constant(pair.actual,count);constant(pair.old,count);
            if(count%(triangles?3:4)==0)pair.same();
            else assertEquals(failure(pair.old::finish),failure(pair.actual::finish));
        }
    }
    @Test void quadBudgetAcceptsLastVertexAndRejectsNext(){
        for(boolean overlay:new boolean[]{false,true}){
            Pair pair=new Pair(overlay,false);constant(pair.actual,262144);constant(pair.old,262144);pair.same();
            pair.actual.vertex(0,0,0);pair.old.vertex(0,0,0);
            assertEquals("Section vertex budget exceeded",failure(pair.actual::finish));
            assertEquals("Section vertex budget exceeded",failure(pair.old::finish));
        }
    }
    @Test void triangleDegenerateVerticesCountTowardBudget(){
        Pair pair=new Pair(true,true);constant(pair.actual,196608);constant(pair.old,196608);pair.same();
        assertEquals(262144*28,pair.actual.finish().length);
        pair.actual.vertex(0,0,0);pair.old.vertex(0,0,0);
        assertEquals(failure(pair.old::finish),failure(pair.actual::finish));
    }
}
