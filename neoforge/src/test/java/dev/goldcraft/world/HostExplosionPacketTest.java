package dev.goldcraft.world;

import java.util.*;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostExplosionPacketTest {
    @Test void finalModListIsAuthoritativeAndDuplicateIndependent() {
        var center=new Vec3d(.5,64.5,.5);
        var kept=new BlockPos(0,64,0);var cancelled=new BlockPos(1,64,0);var distant=new BlockPos(99,64,0);
        var reached=Set.of(kept,cancelled,distant);
        assertTrue(HostExplosionPacket.finalCells(center,4,reached,List.of()).isEmpty());
        var result=HostExplosionPacket.finalCells(center,4,reached,List.of(kept,kept,distant,new BlockPos(2,64,0)));
        assertEquals(1,result.size());assertEquals(0,result.getFirst().x());
        assertEquals(-1,result.getFirst().y());assertEquals(0,result.getFirst().z());
        assertTrue(HostExplosionPacket.finalCells(center,Float.NaN,reached,List.of(kept)).isEmpty());
    }
}
