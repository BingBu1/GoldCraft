package dev.goldcraft.bridge;

import java.util.LinkedHashMap;

/** Ordered damage receipts keep delayed host snapshots from replaying a status hurt. */
public final class NativeHealthLedger {
    private final LinkedHashMap<Long,Float> pending=new LinkedHashMap<>();
    private float health;
    private long barrier=-1,receiptBarrier=-1,acknowledged,lastSent;
    public NativeHealthLedger(float initial){health=initial;}
    public void sent(long event,float delta){
        if(delta==0)throw new IllegalArgumentException("Empty status health change");
        reserve(event,delta);
    }
    /** Legacy melee/skill feedback stays unchanged until its native receipt. */
    public void awaitReceipt(long event){reserve(event,0);}
    private void reserve(long event,float delta){
        if(event<=lastSent||event<=acknowledged||!Float.isFinite(delta))
            throw new IllegalArgumentException("Invalid pending native health change");
        pending.put(event,delta);lastSent=event;
    }
    public void receipt(long event,float actual,long snapshotTick){
        if(!Float.isFinite(actual)||actual<0||snapshotTick<0)throw new IllegalArgumentException("Invalid native health receipt");
        if(event<=acknowledged||!pending.containsKey(event))return;
        acknowledged=event;health=actual;barrier=Math.max(barrier,snapshotTick);receiptBarrier=Math.max(receiptBarrier,snapshotTick);
        pending.entrySet().removeIf(e->e.getKey()<=event);
    }
    public void snapshot(float actual,long tick){
        if(!Float.isFinite(actual)||actual<0)throw new IllegalArgumentException("Invalid native health snapshot");
        // An actor packet has no damage ack. Until receipts arrive it cannot
        // distinguish damage already reflected in this value from pending work.
        if(pending.isEmpty()&&tick>barrier){health=actual;barrier=tick;}
    }
    public float health(float maximum){
        float value=health;
        for(float delta:pending.values())value=Math.clamp(value+delta,0,maximum);
        return Math.clamp(value,0,maximum);
    }
    public int pending(){return pending.size();}
    public boolean settled(long tick){return pending.isEmpty()&&tick>receiptBarrier;}
}
