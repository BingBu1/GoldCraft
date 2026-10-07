package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class VitalsLedgerTest {
    @Test void concurrentNativeAndMinecraftDamageAreBothRetained(){
        var ledger=new VitalsLedger();ledger.snapshot(100,0);
        ledger.add(new VitalsLedger.Change(1,-25,new byte[0]));
        ledger.snapshot(80,0);assertEquals(55,ledger.health(100));
        ledger.snapshot(55,1);assertEquals(55,ledger.health(100));assertEquals(0,ledger.size());
        ledger.snapshot(55,1);assertEquals(55,ledger.health(100));
    }
    @Test void healingAndPendingDamageSurviveUnchangedFormSnapshots(){
        var ledger=new VitalsLedger();ledger.snapshot(65,0);
        ledger.add(new VitalsLedger.Change(10,-5,new byte[0]));
        ledger.add(new VitalsLedger.Change(11,10,new byte[0]));
        for(int i=0;i<3;i++){ledger.snapshot(65,0);assertEquals(70,ledger.health(100));}
        ledger.snapshot(60,10);assertEquals(70,ledger.health(100));
        ledger.snapshot(70,11);assertEquals(70,ledger.health(100));assertEquals(0,ledger.size());
    }
    @Test void RejectedLethalEventDoesNotLeaveTheMinecraftPlayerDead(){
        var ledger=new VitalsLedger();ledger.snapshot(20,0);
        ledger.add(new VitalsLedger.Change(2,-20,new byte[0]));assertEquals(0,ledger.health(100));
        ledger.snapshot(20,2);assertEquals(20,ledger.health(100));
        assertThrows(IllegalArgumentException.class,()->ledger.snapshot(100,1));
        assertThrows(IllegalArgumentException.class,()->ledger.add(new VitalsLedger.Change(2,-5,new byte[0])));
    }
}
