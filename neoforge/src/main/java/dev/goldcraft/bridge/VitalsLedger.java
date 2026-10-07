package dev.goldcraft.bridge;

import java.util.ArrayDeque;
import java.util.List;

/** Reconcile host health without overwriting MC changes not yet acknowledged. */
public final class VitalsLedger {
    public record Change(long sequence,float delta,byte[] payload) {}
    private final ArrayDeque<Change> pending=new ArrayDeque<>();
    private float authority;
    private long acknowledged;
    public void add(Change change){
        if(change.sequence()==0||!Float.isFinite(change.delta())||change.delta()==0
            ||Long.compareUnsigned(change.sequence(),acknowledged)<=0
            ||(!pending.isEmpty()&&Long.compareUnsigned(change.sequence(),pending.getLast().sequence())<=0))
            throw new IllegalArgumentException("Invalid health event order");
        pending.addLast(change);
    }
    public void snapshot(float health,long ack){
        if(!Float.isFinite(health)||Long.compareUnsigned(ack,acknowledged)<0)
            throw new IllegalArgumentException("Stale vitals acknowledgement");
        authority=health;acknowledged=ack;
        while(!pending.isEmpty()&&Long.compareUnsigned(pending.getFirst().sequence(),ack)<=0)pending.removeFirst();
    }
    public float health(float maximum){
        float result=authority;
        for(var change:pending)result=Math.clamp(result+change.delta(),0,maximum);
        return Math.clamp(result,0,maximum);
    }
    public List<Change> pending(){return List.copyOf(pending);}
    public int size(){return pending.size();}
}
