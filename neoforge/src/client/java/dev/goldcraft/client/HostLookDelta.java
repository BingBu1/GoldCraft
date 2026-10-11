package dev.goldcraft.client;

/** Batches explicitly relative INPUT samples without replacing gameplay rotations. */
final class HostLookDelta {
    private double pendingPitch,pendingYaw;
    void sample(float pitch,float yaw,int flags){
        // Native-form, unfocused and stale-control frames are explicit barriers.
        // Absolute angles must never become motion after reacquisition.
        if(flags==3){pendingPitch+=pitch;pendingYaw-=yaw;}
        else discard();
    }
    void discard(){pendingPitch=pendingYaw=0;}
    double pitchDelta(){return pendingPitch;}
    double yawDelta(){return pendingYaw;}
}
