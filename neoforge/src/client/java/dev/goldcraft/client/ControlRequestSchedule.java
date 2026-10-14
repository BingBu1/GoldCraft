package dev.goldcraft.client;

/** Send readiness edges promptly; unchanged leases retain their renewal rate. */
final class ControlRequestSchedule {
    private static final long RENEW_NS=400_000_000L;
    private static final long HANDOFF_RETRY_NS=50_000_000L;
    private long sentAt;
    private boolean sent,ready;

    void reset(){sent=false;sentAt=0;}
    boolean due(long now,boolean nextReady,boolean acknowledged){
        long interval=nextReady&&!acknowledged?HANDOFF_RETRY_NS:RENEW_NS;
        return !sent||ready!=nextReady||now-sentAt>=interval;
    }
    void sent(long now,boolean nextReady){sent=true;sentAt=now;ready=nextReady;}
}
