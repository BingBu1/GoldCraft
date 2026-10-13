package dev.goldcraft.bridge;

/** Absolute deadline in the current same-host Windows QPC domain.
 * Construction validates representation, not authority: the receiver must also
 * verify the authenticated stream, target/life, binding and publication order.
 * No receive-time TTL or nanoTime conversion is permitted.
 */
public record LocalQpcLease(long frequency, long issuedAtMillis,
                            long deadlineMillis, long publication) {
    public static final long MAX_DURATION_MILLIS = 60_000;

    public LocalQpcLease {
        if (frequency <= 0 || issuedAtMillis < 0 || deadlineMillis <= issuedAtMillis ||
            deadlineMillis - issuedAtMillis > MAX_DURATION_MILLIS || publication == 0)
            throw new IllegalArgumentException("Invalid local QPC lease");
    }

    /** Caller supplies a fresh local read, never the peer's historical sample. */
    public boolean liveAt(LocalQpcClock.Sample now) {
        if (now == null || now.frequency() != frequency || now.milliseconds() < 0 ||
            now.marginMillis() < 1 || now.milliseconds() >= deadlineMillis) return false;
        // QPC's documented cross-thread tick ambiguity is already converted
        // conservatively by this clock. Do not accept a distant future issue.
        return issuedAtMillis <= now.milliseconds() ||
               issuedAtMillis - now.milliseconds() <= now.marginMillis();
    }
}
