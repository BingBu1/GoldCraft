package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class NativeTntOriginTest {
    private static final UUID PLAYER=UUID.fromString("747e1ae2-3a83-3ddb-8cf2-5e0cadb6c7a0");
    private static HostWorldState world(long epoch) {
        var host=new HostWorldState();host.world(new Wire.Writer().i64(epoch).string("cs_assault").toByteArray());return host;
    }
    private static void snapshot(HostWorldState host,long tick,int serial,int spawn,int life,int flags,UUID player) {
        var w=new Wire.Writer().i64(host.epoch()).i64(tick).f32(1).i32(0).i32(1)
            .i32(2).i32(serial).i32(4).i32(1).i32(flags).i32(life).f32(100).f32(0);
        for(float v:new float[]{320,-640,36,0,0,0,0,0,-16,-16,-36,16,16,36})w.f32(v);
        host.actors(w.bytes(Wire.uuid(player)).i32(spawn).i64(0).toByteArray());
    }
    @Test void sameSpawnFormChangesAndDeathKeepCredit() {
        var host=world(12);var origin=new NativeTntOrigin(12,2,3,5,PLAYER);
        snapshot(host,1,3,5,7,17,PLAYER);assertNotNull(origin.resolve(host));
        snapshot(host,2,3,5,8,113,PLAYER);assertTrue(origin.resolve(host).minecraftForm());
        snapshot(host,3,3,5,9,17,PLAYER);assertFalse(origin.resolve(host).minecraftForm());
        snapshot(host,4,3,5,10,16,PLAYER);assertNotNull(origin.resolve(host));
    }
    @Test void newNativeSpawnCannotInheritOldTnt() {
        var host=world(12);var origin=new NativeTntOrigin(12,2,3,5,PLAYER);
        snapshot(host,1,3,5,7,17,PLAYER);assertNotNull(origin.resolve(host));
        snapshot(host,2,3,6,8,17,PLAYER);assertNull(origin.resolve(host));
        assertNotNull(new NativeTntOrigin(12,2,3,6,PLAYER).resolve(host));
    }
    @Test void replacementConnectionAndMissingActorRejectOldCredit() {
        var host=world(12);var origin=new NativeTntOrigin(12,2,3,5,PLAYER);
        assertNull(origin.resolve(host));
        snapshot(host,1,4,5,7,17,PLAYER);assertNull(origin.resolve(host));
    }
    @Test void pairingLossOrReplacementRejectsPairedOrigin() {
        var host=world(12);var origin=new NativeTntOrigin(12,2,3,5,PLAYER);
        snapshot(host,1,3,5,7,1,PLAYER);assertNull(origin.resolve(host));
        snapshot(host,2,3,5,7,17,new UUID(0,1));assertNull(origin.resolve(host));
    }
    @Test void nativeOriginDoesNotRequireMinecraftPairing() {
        var host=world(12);var origin=new NativeTntOrigin(12,2,3,5,null);
        snapshot(host,1,3,5,7,1,new UUID(0,0));assertNotNull(origin.resolve(host));
        snapshot(host,2,3,6,8,1,new UUID(0,0));assertNull(origin.resolve(host));
    }
    @Test void mapRestartRejectsEvenMatchingSlotSerialAndSpawn() {
        var host=world(13);snapshot(host,1,3,5,7,17,PLAYER);
        assertNull(new NativeTntOrigin(12,2,3,5,PLAYER).resolve(host));
        assertNull(new NativeTntOrigin(0,2,3,5,PLAYER).resolve(host));
    }
}
