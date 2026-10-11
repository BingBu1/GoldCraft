package dev.goldcraft.client;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostEpicTargetsTest {
    private static final UUID PLAYER=new UUID(1,2);
    private static final UUID NONE=new UUID(0,0);
    private static HostWorldState host(){
        var host=new HostWorldState();host.world(new Wire.Writer().i64(12).string("cs_assault").toByteArray());return host;
    }
    private static void actor(Wire.Writer w,int slot,int serial,int team,int flags,int life,float health,UUID player){
        w.i32(slot).i32(serial).i32(slot).i32(team).i32(flags).i32(life).f32(health).f32(0);
        for(float value:new float[]{320,-640,36,0,0,0,0,0,-16,-16,-36,16,16,36})w.f32(value);
        w.bytes(Wire.uuid(player)).i32(life).i64(0);
    }
    private static void snapshot(HostWorldState host,long tick,int ownTeam,int ownFlags,int serial,int team,int flags,int life,float health){
        var w=new Wire.Writer().i64(host.epoch()).i64(tick).f32(1).i32(0).i32(2);
        actor(w,1,3,ownTeam,ownFlags,5,100,PLAYER);actor(w,2,serial,team,flags,life,health,NONE);host.actors(w.toByteArray());
    }
    private static boolean target(HostWorldState host,long epoch,int serial,int life){return HostEpicTargets.canTarget(host,host.actor(PLAYER),epoch,2,serial,life);}

    @Test void opposingFactionsOnlyAndTeamChangesImmediatelyInvalidate(){
        var host=host();snapshot(host,1,2,113,8,1,1,6,100);assertTrue(target(host,12,8,6));
        snapshot(host,2,2,113,8,2,1,6,100);assertFalse(target(host,12,8,6));
        snapshot(host,3,1,113,8,2,1,6,100);assertTrue(target(host,12,8,6));
        snapshot(host,4,1,113,8,0,1,6,100);assertFalse(target(host,12,8,6));
        snapshot(host,5,1,113,8,3,1,6,100);assertFalse(target(host,12,8,6));
        assertFalse(HostEpicTargets.canTarget(host,host.actor(PLAYER),12,1,3,5));
    }
    @Test void respawnReconnectMapChangeAndOldSnapshotsCannotReuseLockedIdentity(){
        var host=host();snapshot(host,1,2,113,8,1,1,6,100);assertTrue(target(host,12,8,6));
        snapshot(host,2,2,113,8,1,0,6,0);assertFalse(target(host,12,8,6));
        snapshot(host,3,2,113,8,1,1,7,100);assertFalse(target(host,12,8,6));assertTrue(target(host,12,8,7));
        snapshot(host,2,2,113,8,1,1,6,100);assertFalse(target(host,12,8,6));
        snapshot(host,4,2,113,9,1,1,7,100);assertFalse(target(host,12,8,7));assertTrue(target(host,12,9,7));
        host.world(new Wire.Writer().i64(13).string("cs_assault").toByteArray());
        snapshot(host,1,2,113,9,1,1,7,100);assertFalse(target(host,12,9,7));assertTrue(target(host,13,9,7));
    }
    @Test void unboundDeadSpectatorAndNativeViewerCannotAcquire(){
        var host=host();snapshot(host,1,2,113,8,1,1,6,100);
        assertFalse(target(host,0,8,6));assertFalse(target(host,12,0,6));assertFalse(target(host,12,8,0));
        assertFalse(HostEpicTargets.canTarget(host,null,12,2,8,6));
        snapshot(host,2,2,17,8,1,1,6,100);assertFalse(target(host,12,8,6));
        snapshot(host,3,2,81,8,1,1,6,100);assertFalse(target(host,12,8,6));
        snapshot(host,4,2,113,8,1,5,6,100);assertFalse(target(host,12,8,6));
        snapshot(host,5,2,113,8,1,1,6,0);assertFalse(target(host,12,8,6));
        snapshot(host,6,3,113,8,1,1,6,100);assertFalse(target(host,12,8,6));
        snapshot(host,7,2,113,8,1,113,6,100);assertFalse(target(host,12,8,6));
        host.actors(new Wire.Writer().i64(12).i64(8).f32(1).i32(0).i32(0).toByteArray());
        assertFalse(target(host,12,8,6));
    }
}
