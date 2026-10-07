package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostWorldStateTest {
    private static byte[] actor(long tick,int life){
        return actor(tick,life,113);
    }
    private static byte[] actor(long tick,int life,int flags){
        var w=new Wire.Writer().i64(12).i64(tick).f32(1).i32(0).i32(1)
            .i32(2).i32(3).i32(4).i32(1).i32(flags).i32(life).f32(100).f32(0);
        for(float value:new float[]{320,-640,36,0,0,0,0,0,-16,-16,-36,16,16,36})w.f32(value);
        return w.bytes(Wire.uuid(java.util.UUID.fromString("747e1ae2-3a83-3ddb-8cf2-5e0cadb6c7a0"))).i32(2).i64(0).toByteArray();
    }
    @Test void nativeFormRetainsPairingButRevokesMovementAndCannotBeRestoredByOldSnapshot(){
        var host=new HostWorldState();host.world(new Wire.Writer().i64(12).string("cs_assault").toByteArray());
        host.actors(actor(1,4,113));assertTrue(host.actors().getFirst().minecraftForm());
        host.actors(actor(2,5,17));var nativeActor=host.actors().getFirst();
        assertFalse(nativeActor.minecraftForm());assertNotNull(host.actor(nativeActor.minecraftPlayer()));
        assertEquals(0,nativeActor.flags()&32);assertEquals(5,nativeActor.life());
        host.actors(actor(1,4,113));assertFalse(host.actors().getFirst().minecraftForm());
        assertThrows(IllegalArgumentException.class,()->host.actors(actor(3,6,128)));
    }
    @Test void lifeChangesWithoutReconnectAndOldSnapshotsCannotRestorePreviousSpawn(){
        var host=new HostWorldState();host.world(new Wire.Writer().i64(12).string("cs_assault").toByteArray());
        host.actors(actor(1,5));var old=host.actors().getFirst();
        host.actors(actor(2,6));var next=host.actors().getFirst();
        assertEquals(old.serial(),next.serial());assertEquals(6,next.life());
        assertEquals(new HostWorldState.Vector(10,64,20),next.minecraftFeet());
        host.actors(actor(1,5));assertEquals(6,host.actors().getFirst().life());
        byte[] truncated=java.util.Arrays.copyOf(actor(3,7),128);
        assertThrows(IllegalArgumentException.class,()->host.actors(truncated));
        assertEquals(6,host.actors().getFirst().life());
    }
    private static byte[] brush(long epoch,long tick,int flags) {
        var w=new Wire.Writer().i64(epoch).i64(tick).i32(1).i32(70).i32(3).i32(13).i32(7).i32(flags);
        for(int i=0;i<15;i++)w.f32(0);return w.toByteArray();
    }
    @Test void ladderIsASeparateNonSolidVolumeAndOldSnapshotsCannotRestoreIt() {
        var host=new HostWorldState();host.world(new Wire.Writer().i64(12).string("cs_assault").toByteArray());
        host.brushes(brush(12,1,2));assertTrue(host.brushes().getFirst().ladder());assertFalse(host.brushes().getFirst().solid());
        host.brushes(new Wire.Writer().i64(12).i64(2).i32(0).toByteArray());assertTrue(host.brushes().isEmpty());
        host.brushes(brush(12,1,2));assertTrue(host.brushes().isEmpty());
        host.brushes(brush(11,9,2));assertTrue(host.brushes().isEmpty());
        assertThrows(IllegalArgumentException.class,()->host.brushes(brush(12,3,4)));
    }
}
