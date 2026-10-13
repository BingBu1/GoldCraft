package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** Same-machine QPC mapping response. An ACK is not proof until the native peer
 * validates it and returns READY on the authenticated server connection. */
public final class LocalQpcProof {
    public static final int LAYOUT = 1, MAGIC = 0x31514347;
    public static final int MAPPING_BYTES = 80, ACK_BYTES = 40;
    private static final int FILE_MAP_READ = 0x0004;
    private static final byte[] NAME_PREFIX = "Local\\GoldCraftClock-".getBytes(StandardCharsets.US_ASCII);
    private static final int NAME_BYTES = NAME_PREFIX.length + 32;

    private LocalQpcProof() {}

    public static final class Response {
        private final byte[] ack;
        private final boolean accepted;
        private final long frequency;

        private Response(long epoch, long frequency, byte[] challenge, boolean accepted) {
            this.accepted = accepted;
            this.frequency = accepted ? frequency : 0;
            ByteBuffer reply = ByteBuffer.allocate(ACK_BYTES).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(LAYOUT).putInt(accepted ? 1 : 0).putLong(epoch).putLong(this.frequency);
            if (accepted) reply.put(challenge);
            ack = reply.array(); // Declines retain zero frequency and zero challenge.
        }

        public byte[] ack() { return ack.clone(); }
        public boolean accepted() { return accepted; }
        public long frequency() { return frequency; }
    }

    private record Offer(long epoch, long frequency, long issued, long deadline, String name) {
        static Offer parse(byte[] payload, long expectedEpoch) {
            if (payload == null) throw new IllegalArgumentException("Clock offer missing");
            Wire.Reader reader = new Wire.Reader(payload);
            if (reader.i32() != LAYOUT || reader.i32() != MAPPING_BYTES)
                throw new IllegalArgumentException("Clock offer layout");
            long epoch = reader.i64(), frequency = reader.i64();
            long issued = reader.i64(), deadline = reader.i64();
            int nameLength = reader.u16();
            if (nameLength != NAME_BYTES) throw new IllegalArgumentException("Clock mapping name length");
            byte[] name = reader.bytes(nameLength);
            reader.finish();
            for (int i = 0; i < NAME_PREFIX.length; ++i)
                if (name[i] != NAME_PREFIX[i]) throw new IllegalArgumentException("Clock mapping name");
            for (int i = NAME_PREFIX.length; i < name.length; ++i) {
                int value = Byte.toUnsignedInt(name[i]);
                if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
                    throw new IllegalArgumentException("Clock mapping name");
            }
            if (expectedEpoch == 0 || epoch != expectedEpoch || frequency <= 0 || issued < 0 || deadline <= issued)
                throw new IllegalArgumentException("Clock offer authority/time");
            long duration = deadline - issued; // Both QPC values are nonnegative signed longs.
            if (frequency <= Long.MAX_VALUE / 3 && duration > frequency * 3)
                throw new IllegalArgumentException("Clock offer duration");
            return new Offer(epoch, frequency, issued, deadline, new String(name, StandardCharsets.US_ASCII));
        }
    }

    interface Mapping extends AutoCloseable {
        ByteBuffer buffer();
        @Override void close();
    }

    @FunctionalInterface interface Opener {
        Mapping open(String name);
    }

    public static boolean supported(int role, int hostRole) {
        return role == Wire.FABRIC_SERVER && hostRole == Wire.HOST_SERVER && available();
    }

    private static boolean available() {
        try {
            if (!System.getProperty("os.name", "").startsWith("Windows") ||
                "0".equals(System.getenv("GOLDCRAFT_LOCAL_QPC"))) return false;
            String version = com.sun.jna.Native.class.getPackage().getImplementationVersion();
            return com.sun.jna.Native.POINTER_SIZE == 8 &&
                ("5.14.0".equals(version) || "5.14.0 (b0)".equals(version)) &&
                LocalQpcClock.system().failure() == LocalQpcClock.Failure.NONE;
        } catch (LinkageError | RuntimeException unavailable) {
            return false;
        }
    }

    /** Call only after the socket WELCOME has authenticated expectedEpoch. */
    public static Response respond(byte[] payload, long expectedEpoch) {
        Offer offer = Offer.parse(payload, expectedEpoch);
        if (!available()) return decline(offer.epoch());
        return respond(offer, LocalQpcClock.system(), WindowsMapping::open);
    }

    // Package-only fixture seam. Production always selects the system clock and
    // read-only Windows opener in the two-argument method above.
    static Response respond(byte[] payload, long expectedEpoch, LocalQpcClock clock, Opener opener) {
        return respond(Offer.parse(payload, expectedEpoch), clock, opener);
    }

    private static Response respond(Offer offer, LocalQpcClock clock, Opener opener) {
        if (clock == null || opener == null) throw new IllegalArgumentException("Clock proof dependencies");
        LocalQpcClock.Sample before = clock.read();
        if (!inside(before, offer)) return decline(offer.epoch());
        Mapping mapping;
        try {
            mapping = opener.open(offer.name());
        } catch (LinkageError | SecurityException unavailable) {
            return decline(offer.epoch());
        }
        if (mapping == null) return decline(offer.epoch());
        byte[] challenge;
        try (mapping) {
            ByteBuffer view = mapping.buffer();
            if (view == null || !view.isDirect() || view.capacity() != MAPPING_BYTES || view.limit() < MAPPING_BYTES)
                throw new IllegalArgumentException("Clock mapping size");
            byte[] header = new byte[MAPPING_BYTES];
            // The native peer publishes an immutable map. Copy it once; do not
            // re-read the challenge or retain any mapped-memory reference.
            view.asReadOnlyBuffer().get(0, header);
            challenge = validateHeader(header, offer);
        }
        LocalQpcClock.Sample after = clock.read();
        return inside(after, offer)
            ? new Response(offer.epoch(), offer.frequency(), challenge, true)
            : decline(offer.epoch());
    }

    private static Response decline(long epoch) {
        return new Response(epoch, 0, null, false);
    }

    private static boolean inside(LocalQpcClock.Sample sample, Offer offer) {
        return sample != null && sample.frequency() == offer.frequency() &&
            sample.counter() >= offer.issued() && sample.counter() < offer.deadline();
    }

    private static byte[] validateHeader(byte[] header, Offer offer) {
        ByteBuffer data = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
        if (data.getInt(0) != MAGIC || data.getInt(4) != LAYOUT || data.getInt(8) != MAPPING_BYTES ||
            data.getInt(12) != 0 || data.getLong(16) != offer.epoch() ||
            data.getLong(24) != offer.frequency() || data.getLong(32) != offer.issued() ||
            data.getLong(40) != offer.deadline())
            throw new IllegalArgumentException("Clock mapping header");
        byte[] challenge = new byte[16];
        System.arraycopy(header, 48, challenge, 0, challenge.length);
        boolean nonzero = false;
        for (byte value : challenge) nonzero |= value != 0;
        if (!nonzero) throw new IllegalArgumentException("Clock mapping challenge");
        for (int i = 64; i < MAPPING_BYTES; ++i)
            if (header[i] != 0) throw new IllegalArgumentException("Clock mapping reserved bytes");
        return challenge;
    }

    private static final class WindowsMapping implements Mapping {
        private final com.sun.jna.platform.win32.Kernel32 kernel;
        private final com.sun.jna.platform.win32.WinNT.HANDLE handle;
        private final com.sun.jna.Pointer address;
        private final ByteBuffer buffer;

        private WindowsMapping(com.sun.jna.platform.win32.Kernel32 kernel,
                               com.sun.jna.platform.win32.WinNT.HANDLE handle, com.sun.jna.Pointer address) {
            this.kernel = kernel;
            this.handle = handle;
            this.address = address;
            this.buffer = address.getByteBuffer(0, MAPPING_BYTES).asReadOnlyBuffer();
        }

        static Mapping open(String name) {
            com.sun.jna.platform.win32.Kernel32 kernel;
            com.sun.jna.platform.win32.WinNT.HANDLE handle;
            try {
                kernel = com.sun.jna.platform.win32.Kernel32.INSTANCE;
                handle = kernel.OpenFileMapping(FILE_MAP_READ, false, name);
            } catch (LinkageError | RuntimeException unavailable) {
                return null;
            }
            if (handle == null) return null;
            com.sun.jna.Pointer address = null;
            boolean retained = false;
            try {
                address = kernel.MapViewOfFile(handle, FILE_MAP_READ, 0, 0, MAPPING_BYTES);
                if (address == null) return null;
                var mapping = new WindowsMapping(kernel, handle, address);
                retained = true;
                return mapping;
            } catch (LinkageError | RuntimeException unavailable) {
                return null;
            } finally {
                if (!retained) {
                    try { if (address != null) kernel.UnmapViewOfFile(address); }
                    finally { kernel.CloseHandle(handle); }
                }
            }
        }

        @Override public ByteBuffer buffer() { return buffer; }

        @Override public void close() {
            try { kernel.UnmapViewOfFile(address); }
            finally { kernel.CloseHandle(handle); }
        }
    }
}
