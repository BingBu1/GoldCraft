package dev.goldcraft.bridge;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import java.nio.file.Path;
final class NativeStatusLedgerTest {
    @TempDir Path temporary;
    @Test void orderedReceiptsAndBoundedHud() throws Exception {
        NativeStatusProbe.main(new String[]{temporary.resolve("status-hud.bin").toString()});
    }
}
