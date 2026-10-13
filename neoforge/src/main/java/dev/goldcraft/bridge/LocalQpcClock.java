package dev.goldcraft.bridge;

/**
 * Analysis candidate for the two authoritative services on one Windows host.
 * BridgeLink currently connects only to 127.0.0.1. A future remote transport
 * must not advertise this clock capability based only on matching frequency.
 * No System.nanoTime origin or rate is used to interpret native timestamps.
 * Session, target, lifecycle and native-admission validation remain separate.
 */
public final class LocalQpcClock {
    public enum Failure {
        NONE, UNAVAILABLE, READ_FAILED, INVALID_COUNTER, INVALID_FREQUENCY,
        FREQUENCY_CHANGED, COUNTER_REGRESSED, CONVERSION_OVERFLOW
    }

    public record Sample(long counter, long frequency, long milliseconds, int marginMillis) {}

    // Reused by the serialized reader; no cross-process pointer is transmitted.
    static final class Raw {
        long counter, frequency;
    }
    @FunctionalInterface interface Reader {
        boolean read(Raw output);
    }

    private final Reader reader;
    private final Raw raw = new Raw();
    private long lastCounter, frequency;
    private boolean observed;
    private Failure failure = Failure.NONE;

    LocalQpcClock(Reader reader) {
        this.reader = reader;
        if (reader == null) failure = Failure.UNAVAILABLE;
    }

    private static final class Platform {
        static final LocalQpcClock CLOCK = createWindowsClock();
    }

    /** One fault latch per JVM lifetime; reconnects and map changes do not reset it. */
    public static LocalQpcClock system() { return Platform.CLOCK; }

    private static LocalQpcClock createWindowsClock() {
        try {
            if (!System.getProperty("os.name", "").startsWith("Windows")) return new LocalQpcClock(null);
            String version = com.sun.jna.Native.class.getPackage().getImplementationVersion();
            if (com.sun.jna.Native.POINTER_SIZE != 8 ||
                !("5.14.0".equals(version) || "5.14.0 (b0)".equals(version))) return new LocalQpcClock(null);
            return new LocalQpcClock(new WindowsReader());
        } catch (LinkageError | RuntimeException unavailable) {
            return new LocalQpcClock(null);
        }
    }

    public synchronized Failure failure() { return failure; }

    /** Returns null after any fault. Old successful readings are never replayed. */
    public synchronized Sample read() {
        if (failure != Failure.NONE) return null;
        try {
            if (!reader.read(raw)) return failed(Failure.READ_FAILED);
        } catch (LinkageError | RuntimeException readFailure) {
            return failed(Failure.READ_FAILED);
        }
        if (raw.counter < 0) return failed(Failure.INVALID_COUNTER);
        if (raw.frequency <= 0) return failed(Failure.INVALID_FREQUENCY);
        if (observed && raw.frequency != frequency) return failed(Failure.FREQUENCY_CHANGED);
        if (observed && raw.counter < lastCounter) return failed(Failure.COUNTER_REGRESSED);
        long whole = raw.counter / raw.frequency;
        // This Java endpoint deliberately supports only positive signed ms.
        // Native uint64 values above this limit must not enable the capability.
        if (whole > Long.MAX_VALUE / 1000) return failed(Failure.CONVERSION_OVERFLOW);
        long base = whole * 1000;
        int fraction = fractionMillis(raw.counter % raw.frequency, raw.frequency);
        if (base > Long.MAX_VALUE - fraction) return failed(Failure.CONVERSION_OVERFLOW);
        int margin = (int)(1 + 2000 / raw.frequency + (2000 % raw.frequency == 0 ? 0 : 1));
        lastCounter = raw.counter;
        frequency = raw.frequency;
        observed = true;
        return new Sample(lastCounter, frequency, base + fraction, margin);
    }

    private Sample failed(Failure reason) {
        failure = reason;
        return null;
    }

    // Same exact integer quotient as native-clock-04; fallback arithmetic treats
    // the intermediate sum as unsigned, always smaller than 2 * divisor.
    static int fractionMillis(long remainder, long divisor) {
        if (remainder <= Long.MAX_VALUE / 1000) return (int)(remainder * 1000 / divisor);
        long residue = 0;
        int quotient = 0;
        for (int bit = 9; bit >= 0; --bit) {
            residue *= 2;
            quotient *= 2;
            if (Long.compareUnsigned(residue, divisor) >= 0) { residue -= divisor; ++quotient; }
            if (((1000 >>> bit) & 1) != 0) {
                residue += remainder;
                if (Long.compareUnsigned(residue, divisor) >= 0) { residue -= divisor; ++quotient; }
            }
        }
        return quotient;
    }

    // Exact JNA 5.14.0 Kernel32 lacks these declarations. LARGE_INTEGER* is an
    // eight-byte signed result; NativeLongByReference would be wrong on Win64.
    public interface QpcKernel extends com.sun.jna.win32.StdCallLibrary {
        boolean QueryPerformanceCounter(com.sun.jna.ptr.LongByReference result);
        boolean QueryPerformanceFrequency(com.sun.jna.ptr.LongByReference result);
    }

    private static final class WindowsReader implements Reader {
        private final QpcKernel kernel = com.sun.jna.Native.load("kernel32", QpcKernel.class);
        private final com.sun.jna.ptr.LongByReference counter = new com.sun.jna.ptr.LongByReference();
        private final com.sun.jna.ptr.LongByReference frequency = new com.sun.jna.ptr.LongByReference();

        @Override public boolean read(Raw output) {
            if (!kernel.QueryPerformanceFrequency(frequency) || !kernel.QueryPerformanceCounter(counter)) return false;
            output.counter = counter.getValue();
            output.frequency = frequency.getValue();
            return true;
        }
    }
}
