package dev.goldcraft.client;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostLookDeltaTest {
    @Test void batchesEveryHostSampleAndConsumesOnce(){
        var look=new HostLookDelta();look.sample(1,2,3);look.sample(4,8,3);
        assertEquals(-10,look.yawDelta());assertEquals(5,look.pitchDelta());
        look.discard();assertEquals(0,look.yawDelta());assertEquals(0,look.pitchDelta());
        look.sample(0,0,3);assertEquals(0,look.yawDelta());
    }
    @Test void noPitchSaturationAcrossManyFrames(){
        var look=new HostLookDelta();
        for(int i=0;i<90;i++)look.sample(4,-5,3);
        assertEquals(360,look.pitchDelta());assertEquals(450,look.yawDelta());
        look.discard();look.sample(-2,3,3);
        assertEquals(-2,look.pitchDelta());assertEquals(-3,look.yawDelta());
    }
    @Test void menusFocusAndLeaseRecoveryDoNotReplayAbsoluteAngles(){
        var look=new HostLookDelta();look.sample(5,10,3);
        look.sample(-89,270,1);assertEquals(0,look.yawDelta());assertEquals(0,look.pitchDelta());
        look.sample(0,0,3);assertEquals(0,look.yawDelta());
        look.sample(2,4,3);assertEquals(-4,look.yawDelta());
        look.sample(0,0,2);assertEquals(0,look.yawDelta());
        look.sample(1,1,3);assertEquals(-1,look.yawDelta());
        look.discard();look.sample(0,0,3);assertEquals(0,look.yawDelta());
    }
}
