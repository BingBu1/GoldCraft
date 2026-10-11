package dev.goldcraft.client;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ModMeshCaptureTest {
    public static class Config {
        public static boolean compute,maid;
        public static int incompatible;
        public static final boolean immutable=false;
    }
    @BeforeEach void reset(){Config.compute=true;Config.maid=false;}
    private static ModMeshCapture begin(String maid) throws ReflectiveOperationException {
        return new ModMeshCapture(Config.class.getField("compute"),Config.class.getField(maid));
    }
    @Test void scopesUseCpuVerticesAndRestoreTheOriginalMode() throws ReflectiveOperationException {
        try(var outer=begin("maid")) {
            assertFalse(Config.compute);assertTrue(Config.maid);
            try(var inner=begin("maid")){assertFalse(Config.compute);assertTrue(Config.maid);}
            assertFalse(Config.compute);assertTrue(Config.maid);
        }
        assertTrue(Config.compute);assertFalse(Config.maid);
    }
    @Test void failedSecondFieldCannotLeaveComputeDisabled() {
        assertThrows(IllegalArgumentException.class,()->begin("incompatible"));
        assertTrue(Config.compute);assertFalse(Config.maid);
    }
    @Test void failedWriteAndFailedRestorationStillRestoreEarlierFields() {
        var failure=assertThrows(IllegalStateException.class,()->begin("immutable"));
        assertInstanceOf(IllegalAccessException.class,failure.getCause());
        assertEquals(1,failure.getSuppressed().length);assertTrue(Config.compute);
    }
    @Test void duplicateCloseAndMissingOptionalModsAreHarmless() throws ReflectiveOperationException {
        var scope=begin("maid");scope.close();Config.compute=false;Config.maid=true;scope.close();
        assertFalse(Config.compute);assertTrue(Config.maid);
        try(var absent=new ModMeshCapture(null,null)){assertFalse(Config.compute);assertTrue(Config.maid);}
    }
}
