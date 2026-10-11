package dev.goldcraft.client;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class CaptureCleanupTest {
    @Test void failedFlushStillRestoresEveryStateAndPreservesItsOriginalError() {
        var visited=new ArrayList<String>();
        var flush=new IllegalStateException("flush");var release=new AssertionError("release");
        var matrix=new IllegalArgumentException("matrix");
        var cleanup=new CaptureCleanup();
        cleanup.run(()->{throw flush;});
        cleanup.run(()->{visited.add("release");throw release;});
        cleanup.run(()->{visited.add("matrix");throw matrix;});
        for(String state:List.of("gl","shader","texture","activeUnit"))cleanup.run(()->visited.add(state));
        assertSame(flush,assertThrows(IllegalStateException.class,cleanup::rethrow));
        assertEquals(List.of("release","matrix","gl","shader","texture","activeUnit"),visited);
        assertArrayEquals(new Throwable[]{release,matrix},flush.getSuppressed());
    }
    @Test void initializationFailureRemainsPrimaryEvenIfCleanupThrowsTheSameObject() {
        var failure=new AssertionError("initialization");var cleanup=new CaptureCleanup(failure);
        cleanup.run(()->{throw failure;});cleanup.run(()->{});
        assertSame(failure,assertThrows(AssertionError.class,cleanup::rethrow));assertEquals(0,failure.getSuppressed().length);
    }
}
