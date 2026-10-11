package dev.goldcraft.client;

import dev.goldcraft.bridge.Wire;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class SectionLightSnapshotTest {
    private static SectionLightSnapshot.Lamp light(float x,float y,float z) {
        return new SectionLightSnapshot.Lamp(x,y,z,480,0xff9cd6ff);
    }
    private static List<SectionLightSnapshot.Lamp> section(int sx,int sy,int sz) {
        List<SectionLightSnapshot.Lamp> lamps=new ArrayList<>();
        for(int y=0;y<16;y++)for(int z=0;z<16;z++)for(int x=0;x<16;x++)
            lamps.add(light(sx*16+x+.5f,sy*16+y+.6f,sz*16+z+.5f));
        return lamps;
    }
    private static List<SectionLightSnapshot.Lamp> decode(byte[] bytes,long epoch) {
        Wire.Reader reader=new Wire.Reader(bytes);
        assertEquals(epoch,reader.i64());int count=reader.i32();
        assertEquals(12+20*count,bytes.length);
        List<SectionLightSnapshot.Lamp> lamps=new ArrayList<>();
        for(int i=0;i<count;i++)lamps.add(new SectionLightSnapshot.Lamp(reader.f32(),reader.f32(),reader.f32(),reader.f32(),reader.i32()));
        reader.finish();return lamps;
    }
    @Test void completeFarSectionExceedsAllThreeFormerTruncations() {
        SectionLightSnapshot state=new SectionLightSnapshot();
        var expected=section(2,2,2);state.replace(2,2,2,expected);
        assertEquals(4096,expected.size());
        assertTrue(expected.stream().allMatch(l->l.x()*l.x()+l.y()*l.y()+l.z()*l.z()>24*24));
        assertEquals(expected,decode(state.snapshot(42),42));
    }
    @Test void sectionBoundaryLightsSurviveDifferentDeliveryOrders() {
        SectionLightSnapshot first=new SectionLightSnapshot(),second=new SectionLightSnapshot();
        var left=List.of(light(-.5f,64.6f,15.5f));var right=List.of(light(.5f,64.6f,16.5f));
        first.replace(0,4,1,right);first.replace(-1,4,0,left);
        second.replace(-1,4,0,left);second.replace(0,4,1,right);
        assertArrayEquals(first.snapshot(42),second.snapshot(42));
        assertEquals(List.of(left.getFirst(),right.getFirst()),decode(first.snapshot(42),42));
    }
    @Test void periodicSendsAndIdenticalRescansReuseEncodedBytes() {
        SectionLightSnapshot state=new SectionLightSnapshot();var lamps=section(0,4,0);
        state.replace(0,4,0,lamps);byte[] cached=state.snapshot(42);
        for(int i=0;i<100;i++)assertSame(cached,state.snapshot(42));
        state.replace(0,4,0,new ArrayList<>(lamps));assertSame(cached,state.snapshot(42));
        state.remove(9,9,9);state.replace(9,9,9,List.of());assertSame(cached,state.snapshot(42));
    }
    @Test void changingLampParametersInvalidatesTheSnapshot() {
        SectionLightSnapshot state=new SectionLightSnapshot();state.replace(0,4,0,List.of(light(.5f,64.6f,.5f)));
        byte[] first=state.snapshot(42);
        var changed=new SectionLightSnapshot.Lamp(.5f,64.6f,.5f,224,0xff6688ff);
        state.replace(0,4,0,List.of(changed));byte[] second=state.snapshot(42);
        assertNotSame(first,second);assertEquals(List.of(changed),decode(second,42));
        assertEquals(480,decode(first,42).getFirst().radius());
    }
    @Test void removingBlocksAndUnloadingSectionsRemoveOnlyThoseLights() {
        SectionLightSnapshot state=new SectionLightSnapshot();var full=section(0,4,0);
        var neighbor=light(16.5f,64.6f,.5f);
        state.replace(0,4,0,full);state.replace(1,4,0,List.of(neighbor));
        byte[] initial=state.snapshot(42);state.replace(0,4,0,full.subList(1,full.size()));
        assertEquals(4096,decode(state.snapshot(42),42).size());assertNotSame(initial,state.snapshot(42));
        state.replace(0,4,0,List.of());assertEquals(List.of(neighbor),decode(state.snapshot(42),42));
        state.remove(1,4,0);assertTrue(decode(state.snapshot(42),42).isEmpty());
    }
    @Test void sceneResetAndReconnectDoNotRetainOldLights() {
        SectionLightSnapshot state=new SectionLightSnapshot();state.replace(0,4,0,section(0,4,0));
        byte[] initial=state.snapshot(42);state.clear();
        assertTrue(decode(state.snapshot(42),42).isEmpty());assertNotSame(initial,state.snapshot(42));
        state.replace(2,4,0,List.of(light(32.5f,64.6f,.5f)));state.clear();
        assertTrue(decode(state.snapshot(43),43).isEmpty());
    }
    @Test void epochChangeReencodesHeaderWithoutMutatingPreviouslyQueuedBytes() {
        SectionLightSnapshot state=new SectionLightSnapshot();var lamps=List.of(light(.5f,64.6f,.5f));
        state.replace(0,4,0,lamps);byte[] previous=state.snapshot(42),next=state.snapshot(43);
        assertNotSame(previous,next);assertEquals(lamps,decode(previous,42));assertEquals(lamps,decode(next,43));
        assertSame(next,state.snapshot(43));assertThrows(IllegalArgumentException.class,()->state.snapshot(0));
    }
    @Test void callerCannotChangeCommittedSectionList() {
        SectionLightSnapshot state=new SectionLightSnapshot();var input=new ArrayList<>(List.of(light(.5f,64.6f,.5f)));
        state.replace(0,4,0,input);input.clear();assertEquals(1,decode(state.snapshot(42),42).size());
    }
    @Test void fullExportWindowAndWireBudgetFitWithoutTruncation() {
        SectionLightSnapshot state=new SectionLightSnapshot();
        for(int x=-2;x<=2;x++)for(int y=-2;y<=2;y++)for(int z=-2;z<=2;z++)state.replace(x,y,z,section(x,y,z));
        byte[] snapshot=state.snapshot(42);Wire.Reader reader=new Wire.Reader(snapshot);
        assertEquals(42,reader.i64());assertEquals(512000,reader.i32());
        assertEquals(12+512000*20,snapshot.length);assertTrue(snapshot.length<Wire.MAX_PAYLOAD);
    }
    @Test void globalOverflowRejectsReplacementWithoutChangingAcceptedSnapshot() {
        SectionLightSnapshot state=new SectionLightSnapshot();var full=Collections.nCopies(4096,light(.5f,64.6f,.5f));
        for(int i=0;i<128;i++)state.replace(i,4,0,full);
        byte[] maximum=state.snapshot(42);assertEquals(12+SectionLightSnapshot.MAX_LIGHTS*20,maximum.length);
        assertThrows(IllegalArgumentException.class,()->state.replace(128,4,0,List.of(light(2048.5f,64.6f,.5f))));
        assertSame(maximum,state.snapshot(42));
        state.remove(0,4,0);state.replace(128,4,0,full);
        assertEquals(maximum.length,state.snapshot(42).length);
    }
    @Test void malformedSectionDoesNotReplaceAcceptedState() {
        SectionLightSnapshot state=new SectionLightSnapshot();var lamp=light(.5f,64.6f,.5f);
        state.replace(0,4,0,List.of(lamp));byte[] previous=state.snapshot(42);
        assertThrows(IllegalArgumentException.class,()->state.replace(0,4,0,Collections.nCopies(4097,lamp)));
        assertThrows(NullPointerException.class,()->state.replace(0,4,0,Collections.singletonList(null)));
        assertSame(previous,state.snapshot(42));
        assertThrows(IllegalArgumentException.class,()->new SectionLightSnapshot.Lamp(Float.NaN,0,0,32,0));
        assertThrows(IllegalArgumentException.class,()->new SectionLightSnapshot.Lamp(0,0,0,Float.POSITIVE_INFINITY,0));
        assertThrows(IllegalArgumentException.class,()->new SectionLightSnapshot.Lamp(0,0,0,0,0));
    }
}
