package dev.goldcraft.bridge;
import java.util.*;
import java.nio.file.*;

public final class NativeStatusProbe {
    private static int checks;
    static void check(boolean value){checks++;if(!value)throw new AssertionError("Check "+checks);}
    static void rejects(Runnable r){try{r.run();throw new AssertionError("Accepted malformed state");}catch(IllegalArgumentException expected){checks++;}}
    public static void main(String[] args)throws Exception{
        var health=new NativeHealthLedger(3);
        health.snapshot(3,10);health.sent(1,-1);health.sent(2,-1);
        check(health.health(20)==1);check(!health.settled(100));
        // Snapshot generated after the first hit, delivered before its receipt.
        health.snapshot(2,11);check(health.health(20)==1);
        health.receipt(1,2,11);check(health.health(20)==1);
        health.snapshot(2,11);check(health.health(20)==1);
        // Native denies the second hit: receipt restores the missing point.
        health.receipt(2,2,12);check(health.health(20)==2);
        health.snapshot(3,10);check(health.health(20)==2);check(!health.settled(12));
        health.snapshot(2,13);check(health.settled(13));
        // A heal followed by poison must recover from a denied heal in order.
        health.sent(3,5);health.sent(4,-1);check(health.health(20)==6);
        health.receipt(3,2,14);check(health.health(20)==1);
        health.receipt(4,1,14);check(health.health(20)==1);
        health.receipt(3,7,14);check(health.health(20)==1);
        // Unsent queue failures never alter the ledger; healed amount is capped.
        check(health.pending()==0);health.sent(5,100);check(health.health(20)==20);
        health.receipt(5,20,15);health.sent(6,-100);check(health.health(20)==0);
        health.receipt(6,20,16);check(health.health(20)==20);
        rejects(()->health.sent(6,-1));rejects(()->health.sent(7,Float.NaN));
        rejects(()->health.receipt(7,Float.NaN,17));
        health.awaitReceipt(7);check(health.health(20)==20);check(!health.settled(20));
        health.receipt(7,12,20);check(health.health(20)==12);
        var entries=List.of(new NativeStatusHud.Entry("minecraft:poison",1,59),new NativeStatusHud.Entry("efn:curse_of_blood",0,-1));
        byte[] data=NativeStatusHud.encode(1,2,3,4,5,entries);
        var r=new Wire.Reader(data);check(r.i64()==1);check(r.i64()==2);check(r.i32()==3);check(r.i32()==4);check(r.i32()==5);check(r.i32()==2);
        for(var e:entries){check(r.string(96).equals(e.id()));check(r.i32()==e.amplifier());check(r.i32()==e.duration());}r.finish();
        check(NativeStatusHud.encode(1,3,3,4,5,List.of()).length==32);
        rejects(()->NativeStatusHud.encode(1,1,1,1,1,Collections.nCopies(33,entries.getFirst())));
        for(String id:List.of("x:y\n","a:b:c","a:中","a/b:c","a:",":a","a:"+"b".repeat(95)))
            rejects(()->NativeStatusHud.encode(1,1,1,1,1,List.of(new NativeStatusHud.Entry(id,0,20))));
        for(int level:new int[]{-1,256})rejects(()->NativeStatusHud.encode(1,1,1,1,1,List.of(new NativeStatusHud.Entry("a:b",level,20))));
        rejects(()->NativeStatusHud.encode(1,1,1,1,1,List.of(new NativeStatusHud.Entry("a:b",0,-2))));
        Files.write(Path.of(args[0]),data);
        System.out.println("NativeStatusProbe checks="+checks);
    }
}
