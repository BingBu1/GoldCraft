package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class FramePacerTest {
    @Test void preservesSixtyHertzFramesWithAlternatingRenderTime(){
        var pacing=new FramePacer(60);int emitted=0;
        // The former "15 ms since last render" limiter loses each early frame here.
        for(int i=0;i<120;i++)if(pacing.ready(i*16_666_667L+(i%2==0?0:1_700_000L)))emitted++;
        assertEquals(120,emitted);
    }
    @Test void boundsFastProducersAndRecoversFromResourceReload(){
        var pacing=new FramePacer(60);int emitted=0;
        for(int i=0;i<1000;i++)if(pacing.ready(i*1_000_000L))emitted++;
        assertTrue(emitted>=59&&emitted<=61);
        assertTrue(pacing.ready(6_000_000_000L));
        assertFalse(pacing.ready(6_001_000_000L));
        pacing.reset();assertTrue(pacing.ready(6_002_000_000L));
    }
}
