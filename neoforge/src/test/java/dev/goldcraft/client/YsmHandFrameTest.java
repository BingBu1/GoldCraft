package dev.goldcraft.client;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class YsmHandFrameTest {
    /** Same once-per-frame gate as YSM 2.6.5's RenderHandEvent subscriber. */
    private static final class HandEvents implements YsmHandFrame.Hooks {
        boolean available=true,rendered,failReset;
        int draws,reads,writes;
        public boolean available(){return available;}
        public boolean rendered(){reads++;return rendered;}
        public void rendered(boolean value){
            rendered=value;writes++;
            if(!value&&failReset){failReset=false;throw new IllegalStateException("reset failed");}
        }
        void handEvent(){if(!rendered){rendered=true;draws++;}}
    }

    @Test void successiveHostedFramesEachAllowOnlyOneOfTheTwoHandEvents() {
        var events=new HandEvents();var frame=new YsmHandFrame(events);
        // A prior standalone or hosted pass can leave the actual flag true.
        events.handEvent();events.handEvent();assertEquals(1,events.draws);
        for(int i=0;i<3;i++) {
            assertTrue(frame.begin());
            try {events.handEvent();events.handEvent();}
            finally {frame.end();}
            assertTrue(events.rendered,"restore the caller's consumed guard");
        }
        assertEquals(4,events.draws,"each new HUD frame must reset the skipped world-stage gate");
    }

    @Test void nestedHandPassCannotResetTheOuterPassGuard() {
        var events=new HandEvents();var frame=new YsmHandFrame(events);
        assertTrue(frame.begin());
        events.handEvent();
        boolean nested=frame.begin();
        assertFalse(nested);
        try {events.handEvent();}
        finally {if(nested)frame.end();}
        events.handEvent();
        assertEquals(1,events.draws);
        assertEquals(1,events.writes,"nested entry must not clear the guard");
        frame.end();assertFalse(events.rendered);
        frame.end();assertEquals(2,events.writes,"duplicate cleanup must be harmless");
    }

    @Test void ordinaryRenderingAndUnavailableYsmDoNotTouchTheGuard() {
        // This also proves ordinary rendering does not attempt to initialize ModList.
        assertFalse(YsmHandFrame.beginHosted(false));
        YsmHandFrame.endHosted(false);
        var events=new HandEvents();var frame=new YsmHandFrame(events);
        events.available=false;events.rendered=true;
        assertFalse(frame.begin());frame.end();
        assertTrue(events.rendered);assertEquals(0,events.reads);assertEquals(0,events.writes);
    }

    @Test void failedHandDrawRestoresEitherPreviousStateAndAllowsTheNextFrame() {
        for(boolean before:new boolean[]{false,true}) {
            var events=new HandEvents();var frame=new YsmHandFrame(events);
            events.rendered=before;
            var failure=new IllegalArgumentException("hand model failed");
            assertSame(failure,assertThrows(IllegalArgumentException.class,()->{
                assertTrue(frame.begin());
                try {events.handEvent();throw failure;}
                finally {frame.end();}
            }));
            assertEquals(before,events.rendered);
            assertTrue(frame.begin());
            try {events.handEvent();events.handEvent();}
            finally {frame.end();}
            assertEquals(2,events.draws);assertEquals(before,events.rendered);
        }
    }

    @Test void interruptedResetRestoresStateAndReleasesOwnership() {
        var events=new HandEvents();var frame=new YsmHandFrame(events);
        events.rendered=true;events.failReset=true;
        assertThrows(IllegalStateException.class,frame::begin);
        assertTrue(events.rendered);
        assertTrue(frame.begin());
        try {events.handEvent();events.handEvent();}
        finally {frame.end();}
        assertEquals(1,events.draws);assertTrue(events.rendered);
    }
}
