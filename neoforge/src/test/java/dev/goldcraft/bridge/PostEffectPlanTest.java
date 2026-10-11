package dev.goldcraft.bridge;

import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class PostEffectPlanTest {
    private static ParticleSnapshot.Batch batch(){return new ParticleSnapshot.Batch(1,0,new byte[96]);}
    private static PostEffectPlan.Stage mask(int first,int count){return new PostEffectPlan.Stage(1,0,first,count,0,0,0,1.65f,0,0,1.35f,.28f);}
    @Test void includesIndependentMeshAndAllEightParameters(){
        var stage=new PostEffectPlan.Stage(4,150,0,1,.05f,1,.3f,.1f,0,0,0,0);
        byte[] mesh=ParticleSnapshot.encode(123,7,11,2,1,List.of(batch()));
        byte[] encoded=PostEffectPlan.encode(123,7,11,2,1,List.of(stage),List.of(),List.of(batch()));
        Wire.Reader r=new Wire.Reader(encoded);assertEquals(1,r.u8());assertEquals(4,r.u8());assertEquals(150,r.i32());
        assertEquals(0,r.u8());assertEquals(1,r.u8());
        for(float expected:new float[]{.05f,1,.3f,.1f,0,0,0,0})assertEquals(expected,r.f32());
        assertEquals(0,r.u8());assertEquals(mesh.length,r.i32());assertArrayEquals(mesh,r.bytes(mesh.length));r.finish();
    }
    @Test void explicitClearAndScreenOnlyAreValid(){
        assertEquals(42,PostEffectPlan.encode(1,1,1,1,0,List.of(),List.of(),List.of()).length);
        var screen=new PostEffectPlan.Stage(5,100,0,0,.2f,0,0,.3f,12,.4f,0,0);
        assertEquals(81,PostEffectPlan.encode(1,1,1,1,0,List.of(screen),List.of(),List.of()).length);
    }
    @Test void colorDispersionIsMasklessAndPreservesAllUniformSlots(){
        var dispersion=new PostEffectPlan.Stage(6,100,0,0,1.15f,1.075f,1,.25f,.75f,.7f,.7f,1.1f);
        Wire.Reader r=new Wire.Reader(PostEffectPlan.encode(1,1,1,1,0,List.of(dispersion),List.of(),List.of()));
        assertEquals(1,r.u8());assertEquals(6,r.u8());assertEquals(100,r.i32());assertEquals(0,r.u8());assertEquals(0,r.u8());
        for(float value:new float[]{1.15f,1.075f,1,.25f,.75f,.7f,.7f,1.1f})assertEquals(value,r.f32());
        assertEquals(0,r.u8());assertEquals(36,r.i32());r.bytes(36);r.finish();
        assertThrows(IllegalArgumentException.class,()->new PostEffectPlan.Stage(6,100,0,1,1,1,1,.5f,.5f,0,0,0));
        for(float bad:new float[]{0,-1,Float.NaN,Float.POSITIVE_INFINITY})
            assertThrows(IllegalArgumentException.class,()->new PostEffectPlan.Stage(6,100,0,0,bad,1,1,.5f,.5f,0,0,0));
        assertThrows(IllegalArgumentException.class,()->new PostEffectPlan.Stage(7,100,0,0,1,1,1,.5f,.5f,0,0,0));
    }
    @Test void masklessDispersionDoesNotDiscardOrOwnNeighboringMasks(){
        var space=new PostEffectPlan.Stage(2,11,1,1,0,0,0,1.05f,0,0,0,0);
        var dispersion=new PostEffectPlan.Stage(6,100,2,0,1.15f,1.075f,1,.5f,.5f,0,0,0);
        var stages=List.of(mask(0,1),dispersion,space);
        Wire.Reader r=new Wire.Reader(PostEffectPlan.encode(1,1,1,1,2,stages,List.of(0),List.of(batch(),batch())));
        assertEquals(3,r.u8());
        for(int kind:new int[]{1,6,2}){assertEquals(kind,r.u8());r.bytes(38);}
        assertEquals(1,r.u8());assertEquals(0,r.u8());
        assertThrows(IllegalArgumentException.class,()->PostEffectPlan.encode(1,1,1,1,1,List.of(dispersion),List.of(),List.of(batch())));
    }
    @Test void refusesMissingOverlappingAndOutOfRangeMasks(){
        assertThrows(IllegalArgumentException.class,()->PostEffectPlan.encode(1,1,1,1,1,List.of(),List.of(),List.of(batch())));
        assertThrows(IllegalArgumentException.class,()->PostEffectPlan.encode(1,1,1,1,1,List.of(mask(0,1),mask(0,1)),List.of(),List.of(batch())));
        assertThrows(IllegalArgumentException.class,()->PostEffectPlan.encode(1,1,1,1,1,List.of(mask(1,1)),List.of(),List.of(batch())));
        assertThrows(IllegalArgumentException.class,()->mask(0,0));
        assertThrows(IllegalArgumentException.class,()->new PostEffectPlan.Stage(0,0,0,1,0,0,0,0,0,0,0,0));
        assertThrows(IllegalArgumentException.class,()->new PostEffectPlan.Stage(5,0,0,0,Float.NaN,0,0,0,0,0,0,0));
        assertThrows(IllegalArgumentException.class,()->new PostEffectPlan.Stage(5,32768,0,0,0,0,0,0,0,0,0,0));
        assertThrows(IllegalArgumentException.class,()->new PostEffectPlan.Stage(5,-32769,0,0,0,0,0,0,0,0,0,0));
    }
    @Test void keepsActualPollOrderRatherThanSortingOrMerging(){
        var stages=List.of(new PostEffectPlan.Stage(2,11,1,1,0,0,0,1.05f,0,0,0,0),mask(0,1));
        Wire.Reader r=new Wire.Reader(PostEffectPlan.encode(1,1,1,1,2,stages,List.of(0),List.of(batch(),batch())));
        assertEquals(2,r.u8());assertEquals(2,r.u8());assertEquals(11,r.i32());assertEquals(1,r.u8());assertEquals(1,r.u8());
    }
    @Test void depthResetBoundariesAreUniqueAndBelongToBloomMasks(){
        byte[] encoded=PostEffectPlan.encode(1,1,1,1,2,List.of(mask(0,2)),List.of(0,1),List.of(batch(),batch()));
        Wire.Reader r=new Wire.Reader(encoded);r.bytes(40);assertEquals(2,r.u8());assertEquals(0,r.u8());assertEquals(1,r.u8());
        assertThrows(IllegalArgumentException.class,()->PostEffectPlan.encode(1,1,1,1,1,List.of(mask(0,1)),List.of(0,0),List.of(batch())));
        assertThrows(IllegalArgumentException.class,()->PostEffectPlan.encode(1,1,1,1,1,List.of(mask(0,1)),List.of(1),List.of(batch())));
        var air=new PostEffectPlan.Stage(4,150,0,1,0,1,0,.1f,0,0,0,0);
        assertThrows(IllegalArgumentException.class,()->PostEffectPlan.encode(1,1,1,1,1,List.of(air),List.of(0),List.of(batch())));
    }
}
