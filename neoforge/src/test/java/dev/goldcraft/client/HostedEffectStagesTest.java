package dev.goldcraft.client;

import com.google.gson.JsonObject;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.MinecraftClient;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostedEffectStagesTest {
    private static final class Producer implements HostedEffectStages.Hooks {
        boolean hasTrails=true,fail;
        int probes;
        Runnable duringCallback=()->{};
        final List<String> calls=new ArrayList<>();
        public boolean hasTrails(){probes++;return hasTrails;}
        public void afterEntities(MinecraftClient client) {
            calls.add("afterEntities");
            duringCallback.run();
            if(fail)throw new IllegalStateException("snapshot failed");
        }
    }

    @Test void vanillaAndEmptyFramesNeverReplayAWorldProducer() {
        var producer=new Producer();var stages=new HostedEffectStages(producer);
        assertFalse(stages.begin(false,null));
        assertEquals(0,producer.probes);
        producer.hasTrails=false;
        assertFalse(stages.begin(true,null));
        assertTrue(producer.calls.isEmpty());
        producer.hasTrails=true;
        assertTrue(stages.begin(true,null));
        stages.end();
        assertEquals(List.of("afterEntities"),producer.calls);
    }

    @Test void reentrantAndNestedExportsCannotGenerateTheSameStageTwice() {
        var producer=new Producer();var stages=new HostedEffectStages(producer);
        producer.duringCallback=()->assertFalse(stages.begin(true,null));
        assertTrue(stages.begin(true,null));
        assertFalse(stages.begin(true,null));
        producer.calls.add("particles");
        stages.end();stages.end();
        assertEquals(List.of("afterEntities","particles"),producer.calls);
        var data=new JsonObject();stages.appendDiagnostics(data);
        assertEquals(1,data.get("hostedAfterEntitiesFrames").getAsInt());
        assertEquals(1,data.get("hostedEffectStagesCompleted").getAsInt());
        assertFalse(data.get("hostedEffectStageActive").getAsBoolean());
    }

    @Test void callbackFailureReleasesOwnershipForTheNextFrame() {
        var producer=new Producer();var stages=new HostedEffectStages(producer);
        producer.fail=true;
        assertThrows(IllegalStateException.class,()->stages.begin(true,null));
        stages.end();
        producer.fail=false;
        assertTrue(stages.begin(true,null));stages.end();
        var data=new JsonObject();stages.appendDiagnostics(data);
        assertEquals(1,data.get("hostedEffectStageFailures").getAsInt());
        assertEquals(1,data.get("hostedAfterEntitiesFrames").getAsInt());
        assertEquals(1,data.get("hostedEffectStagesCompleted").getAsInt());
    }

    @Test void particleFailureFinallyAllowsFurtherAfterEntityCallbacks() {
        var producer=new Producer();var stages=new HostedEffectStages(producer);
        assertThrows(IllegalArgumentException.class,()->{
            boolean owned=stages.begin(true,null);
            try {throw new IllegalArgumentException("particle renderer failed");}
            finally {if(owned)stages.end();}
        });
        assertTrue(stages.begin(true,null));stages.end();
        assertEquals(List.of("afterEntities","afterEntities"),producer.calls);
    }
}
