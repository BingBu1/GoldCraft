package dev.goldcraft.client;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class YsmAnimationFrameTest {
    private static final class Model implements YsmAnimationFrame.Hooks {
        boolean available=true,world,failBegin,failEnd;
        int queued;
        float delta;
        final List<String> calls=new ArrayList<>();
        public boolean available(){return available;}
        public boolean world(){return world;}
        public void world(boolean value){world=value;calls.add("world="+value);}
        public void begin(float value){
            assertTrue(world,"controllers must observe the world flag during submission");
            calls.add("begin");delta=value;queued++;
            if(failBegin)throw new IllegalStateException("submit failed after queuing a model");
        }
        public void end(){
            assertTrue(world,"queued animation must complete before restoring the flag");
            calls.add("end");queued=0;
            if(failEnd)throw new IllegalArgumentException("completion failed");
        }
    }

    @Test void ordinaryFrameUpdatesControllersWhileDrawingAndDrainsBeforeRestoring() {
        var model=new Model();var frame=new YsmAnimationFrame(model);
        assertTrue(frame.begin(.375f));
        assertTrue(model.world);assertEquals(.375f,model.delta);
        assertFalse(frame.begin(.5f),"a nested export must not advance the model again");
        model.calls.add("draw");frame.end();
        assertEquals(List.of("world=true","begin","draw","end","world=false"),model.calls);
        assertFalse(model.world);assertEquals(0,model.queued);
        frame.end();assertEquals(5,model.calls.size());
        assertTrue(frame.begin(.75f));frame.end();
    }

    @Test void skippedAndUninitializedModelsDoNotCreateAnExtraWorldFrame() {
        assertFalse(YsmAnimationFrame.beginHosted(false,.5f));
        var model=new Model();var frame=new YsmAnimationFrame(model);
        model.available=false;assertFalse(frame.begin(.5f));
        model.available=true;model.world=true;assertFalse(frame.begin(.5f));
        assertTrue(model.world);assertTrue(model.calls.isEmpty());
    }

    @Test void partiallySubmittedFrameAndFailedCompletionStillRestoreTheRawFlag() {
        var model=new Model();var frame=new YsmAnimationFrame(model);
        model.failBegin=true;model.failEnd=true;
        var error=assertThrows(IllegalStateException.class,()->frame.begin(.5f));
        assertEquals(1,error.getSuppressed().length);
        assertFalse(model.world);assertEquals(0,model.queued);
        model.failBegin=false;model.failEnd=false;
        assertTrue(frame.begin(.6f));
        try {throw new IllegalStateException("entity rendering failed");}
        catch(IllegalStateException expected){assertTrue(model.world);}
        finally {frame.end();}
        assertFalse(model.world);assertEquals(0,model.queued);
        assertTrue(frame.begin(.7f));model.failEnd=true;
        assertThrows(IllegalArgumentException.class,frame::end);
        assertFalse(model.world);
    }
}
