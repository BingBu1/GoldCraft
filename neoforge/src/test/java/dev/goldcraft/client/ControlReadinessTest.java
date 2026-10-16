package dev.goldcraft.client;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class ControlReadinessTest {
    @Test void admissionNeverDependsOnInputAgeButRetainsEveryLifecycleBarrier() {
        for(int mask=0;mask<8192;++mask){
            boolean link=(mask&1)==0,geometry=(mask&2)==0,actor=(mask&4)==0,alive=(mask&8)==0,
                dimension=(mask&16)==0,form=(mask&32)==0,world=(mask&512)==0,fresh=(mask&1024)==0,
                changed=(mask&4096)!=0;
            int life=(mask&64)==0?1:0,flags=((mask&128)==0?1:0)|((mask&256)==0?0:4)|((mask&2048)==0?32:0);
            int reasons=ControlReadiness.reasons(link,geometry,actor,alive,dimension,form,life,flags,world,fresh,changed);
            assertEquals(mask,reasons);
            boolean ready=link&&geometry&&actor&&alive&&dimension&&form&&life!=0&&(flags&1)!=0&&(flags&4)==0&&world;
            assertEquals(ready,ControlReadiness.ready(reasons));
            assertEquals(ready&&(flags&32)!=0&&!changed,ControlReadiness.controlling(reasons));
        }
    }
}
