package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HealthPredictionTest {
    @Test void rewardsStayAboveHealingLimitAndHealingNeverDamages(){
        var nativeHealth=new NativeHealthLedger(170);
        var paired=new VitalsLedger();paired.snapshot(170,0);
        assertEquals(170,nativeHealth.health(100));assertEquals(170,paired.health(100));
        nativeHealth.sent(1,10);paired.add(new VitalsLedger.Change(1,10,new byte[0]));
        assertEquals(170,nativeHealth.health(100));assertEquals(170,paired.health(100));
        nativeHealth.sent(2,-15);paired.add(new VitalsLedger.Change(2,-15,new byte[0]));
        assertEquals(155,nativeHealth.health(100));assertEquals(155,paired.health(100));
    }
    @Test void capReductionWaitsForNativeSnapshotAndIsNotAHealthEvent(){
        var nativeHealth=new NativeHealthLedger(120);
        var paired=new VitalsLedger();paired.snapshot(120,0);
        assertEquals(120,nativeHealth.health(100));assertEquals(120,paired.health(100));
        assertEquals(0,nativeHealth.pending());assertEquals(0,paired.size());
        nativeHealth.snapshot(100,5);paired.snapshot(100,0);
        assertEquals(100,nativeHealth.health(100));assertEquals(100,paired.health(100));
    }
    @Test void damageReceiptsOwnTheResultAcrossConcurrentCapChanges(){
        var nativeHealth=new NativeHealthLedger(120);
        nativeHealth.sent(1,-5);nativeHealth.snapshot(100,5);
        assertEquals(115,nativeHealth.health(100));
        nativeHealth.receipt(1,95,6);assertEquals(95,nativeHealth.health(100));
        nativeHealth.snapshot(120,6);assertEquals(95,nativeHealth.health(100));
        nativeHealth.snapshot(90,7);assertEquals(90,nativeHealth.health(100));
        var paired=new VitalsLedger();paired.snapshot(120,0);paired.add(new VitalsLedger.Change(1,-5,new byte[0]));
        paired.snapshot(100,0);assertEquals(95,paired.health(100));
        paired.snapshot(95,1);assertEquals(95,paired.health(100));assertEquals(0,paired.size());
    }
    @Test void belowCapHealingAndHighClassDamageKeepNormalSemantics(){
        assertEquals(100,HealthPrediction.apply(90,30,100));
        assertEquals(0,HealthPrediction.apply(15,-30,100));
        assertEquals(2390,HealthPrediction.apply(2400,-10,2404));
        assertEquals(2404,HealthPrediction.apply(2400,10,2404));
        assertEquals(2500,HealthPrediction.apply(2500,10,2404));
    }
    @Test void lifeSwitchResetsNativePredictionButClassAndRewardsDoNot(){
        var nativeHealth=new NativeHealthLedger(2500);var paired=new VitalsLedger();paired.snapshot(2500,0);
        assertEquals(2500,nativeHealth.health(2400));assertEquals(2500,paired.health(2400));
        nativeHealth.sent(1,-50);paired.add(new VitalsLedger.Change(1,-50,new byte[0]));
        nativeHealth.receipt(1,2450,10);paired.snapshot(2450,1);
        assertEquals(2450,nativeHealth.health(2400));assertEquals(2450,paired.health(2400));
        nativeHealth.snapshot(1000,11);paired.snapshot(1000,1);
        assertEquals(1000,nativeHealth.health(1200));assertEquals(1000,paired.health(1200));
    }
}
