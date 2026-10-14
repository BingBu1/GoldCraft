package dev.goldcraft.client;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class ControlRequestScheduleTest {
    @Test void readinessRecoveryDoesNotWaitForTheRenewalDeadline(){
        var schedule=new ControlRequestSchedule();
        long start=1_000_000_000L;
        assertTrue(schedule.due(start,false,true));schedule.sent(start,false);
        assertFalse(schedule.due(start+49_000_000L,false,true));
        assertTrue(schedule.due(start+50_000_000L,true,true));
        schedule.sent(start+50_000_000L,true);
        assertFalse(schedule.due(start+400_000_000L,true,true));
        assertTrue(schedule.due(start+450_000_000L,true,true));
    }
    @Test void pendingServerConfirmationRetriesWithinOneSimulationTick(){
        var schedule=new ControlRequestSchedule();schedule.sent(1_000_000_000L,true);
        assertFalse(schedule.due(1_049_000_000L,true,false));
        assertTrue(schedule.due(1_050_000_000L,true,false));
        schedule.sent(1_050_000_000L,true);
        assertFalse(schedule.due(1_100_000_000L,true,true));
        assertTrue(schedule.due(1_450_000_000L,true,true));
    }
    @Test void lossAndNewIdentityNotifyImmediatelyWithoutGrantingControl(){
        var schedule=new ControlRequestSchedule();
        schedule.sent(1_000_000_000L,true);
        assertTrue(schedule.due(1_001_000_000L,false,true));
        schedule.sent(1_001_000_000L,false);
        assertFalse(schedule.due(1_002_000_000L,false,true));
        schedule.reset();assertTrue(schedule.due(1_002_000_000L,false,true));
        // A missing channel must not consume the due notification.
        assertTrue(schedule.due(1_003_000_000L,false,true));
    }
}
