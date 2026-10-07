package dev.goldcraft.bridge;

/** A presentation deadline that tolerates normal frame jitter without discarding every other frame. */
public final class FramePacer {
    private final long period,tolerance;
    private long deadline;
    private boolean started;
    public FramePacer(int fps){
        if(fps<1||fps>240)throw new IllegalArgumentException("Presentation rate");
        period=1_000_000_000L/fps;tolerance=period/8;
    }
    public void reset(){started=false;}
    public boolean ready(long now){
        if(!started||now-deadline>period){started=true;deadline=now+period;return true;}
        if(now+tolerance<deadline)return false;
        deadline+=period;return true;
    }
}
