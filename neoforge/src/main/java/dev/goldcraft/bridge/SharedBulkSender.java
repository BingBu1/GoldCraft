package dev.goldcraft.bridge;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** One producer, owned exclusively by the BridgeLink worker; no cross-process pointers. */
final class SharedBulkSender implements AutoCloseable {
    static final int MAGIC = 0x31424347, LAYOUT = 1, CAPACITY = 64 * 1024 * 1024;
    static final int HEADER_BYTES = 256, MIN_PAYLOAD = 64 * 1024;
    static final int READ_CURSOR = 64, WRITE_CURSOR = 128;
    private static final VarHandle INTS = MethodHandles.byteBufferViewVarHandle(int[].class, ByteOrder.LITTLE_ENDIAN);

    record Offer(long epoch, String name) {
        static Offer parse(byte[] payload, long expectedEpoch) {
            Wire.Reader r = new Wire.Reader(payload);
            if (r.i32() != LAYOUT || r.i32() != CAPACITY) throw new IllegalArgumentException("Bulk layout/capacity");
            long epoch = r.i64();
            if (epoch == 0 || epoch != expectedEpoch) throw new IllegalArgumentException("Bulk epoch");
            int length = r.u16();
            if (length > 96) throw new IllegalArgumentException("Bulk name length");
            byte[] name = r.bytes(length);
            r.finish();
            // Decode only after validating ASCII; replacement UTF-8 decoding must not validate a name.
            for (byte b : name) if (b < 0 || b == 0) throw new IllegalArgumentException("Bulk name ASCII");
            String text = new String(name, StandardCharsets.US_ASCII);
            if (!text.matches("Local\\\\GoldCraftBulk-[0-9a-f]{32}")) throw new IllegalArgumentException("Bulk name");
            return new Offer(epoch, text);
        }
    }

    interface Mapping extends AutoCloseable {
        ByteBuffer buffer();
        @Override void close();
    }

    private final Mapping mapping;
    private final ByteBuffer bytes;
    private int writer;
    private long frames, payloadBytes, copyNanos, fullWaits, highWater;
    private boolean closed;

    static boolean supported(int role, int hostRole) {
        return role == Wire.FABRIC_CLIENT && hostRole == Wire.HOST_CLIENT
            && System.getProperty("os.name", "").startsWith("Windows")
            && !"0".equals(System.getenv("GOLDCRAFT_SHARED_MEMORY"));
    }

    /** OS/library unavailability permits TCP fallback; a corrupt opened map is a protocol error. */
    static SharedBulkSender open(Offer offer) {
        Mapping mapping;
        try {
            mapping = WindowsMapping.open(offer.name());
        } catch (LinkageError | SecurityException e) {
            return null;
        }
        if (mapping == null) return null;
        try {
            return new SharedBulkSender(mapping, offer.epoch());
        } catch (RuntimeException | Error e) {
            mapping.close();
            throw e;
        }
    }

    SharedBulkSender(Mapping mapping, long epoch) {
        this.mapping = mapping;
        bytes = mapping.buffer().order(ByteOrder.LITTLE_ENDIAN);
        if (!bytes.isDirect() || bytes.capacity() != HEADER_BYTES + CAPACITY
            || bytes.getInt(0) != MAGIC || bytes.getInt(4) != LAYOUT
            || bytes.getInt(8) != CAPACITY || bytes.getInt(12) != 0 || bytes.getLong(16) != epoch)
            throw new IllegalArgumentException("Bulk map header");
        for (int i = 24; i < HEADER_BYTES; i++)
            if (!(i >= READ_CURSOR && i < READ_CURSOR + 4) && !(i >= WRITE_CURSOR && i < WRITE_CURSOR + 4)
                && bytes.get(i) != 0) throw new IllegalArgumentException("Bulk map reserved bytes");
        writer = (int) INTS.getAcquire(bytes, WRITE_CURSOR);
        int reader = (int) INTS.getAcquire(bytes, READ_CURSOR);
        if (reader != writer || (writer & 7) != 0) throw new IllegalArgumentException("Bulk initial cursors");
    }

    boolean canPublish(int size) {
        if (closed) throw new IllegalStateException("Bulk map closed");
        if (size < MIN_PAYLOAD || size > Wire.MAX_PAYLOAD) throw new IllegalArgumentException("Bulk payload length");
        int reader=(int) INTS.getAcquire(bytes, READ_CURSOR);
        long used = Integer.toUnsignedLong(writer - reader);
        if (used > CAPACITY || (reader & 7) != 0) throw new IllegalArgumentException("Bulk reader cursor");
        if (CAPACITY - used < aligned(size)) { fullWaits++; return false; }
        return true;
    }

    /** Allocate the tiny ref before publishing, so allocation failure never strands a ring reservation. */
    ByteBuffer frame(int type, byte[] payload, long epoch, long sequence) {
        if (!Wire.bulkType(type)) throw new IllegalArgumentException("Bulk payload type");
        if (!canPublish(payload.length)) return null;
        byte[] reference = new Wire.Writer().i32(type).i32(payload.length).i32(writer).i32(0).toByteArray();
        ByteBuffer frame = Wire.frame(Wire.BULK_REF, epoch, sequence, reference);
        long start = System.nanoTime();
        int offset = writer & (CAPACITY - 1);
        int first = Math.min(payload.length, CAPACITY - offset);
        bytes.put(HEADER_BYTES + offset, payload, 0, first);
        if (first != payload.length) bytes.put(HEADER_BYTES, payload, first, payload.length - first);
        writer += aligned(payload.length);
        INTS.setRelease(bytes, WRITE_CURSOR, writer);
        copyNanos += System.nanoTime() - start;
        frames++; payloadBytes += payload.length;
        highWater = Math.max(highWater, Integer.toUnsignedLong(writer - (int) INTS.getAcquire(bytes, READ_CURSOR)));
        return frame;
    }

    static int aligned(int size) { return (size + 7) & ~7; }
    long frames() { return frames; }
    long payloadBytes() { return payloadBytes; }
    long copyNanos() { return copyNanos; }
    long fullWaits() { return fullWaits; }
    long highWater() { return highWater; }
    @Override public void close() { if (!closed) { closed = true; mapping.close(); } }

    /** JNA is resolved only here. Dedicated server and non-Windows links never load Kernel32. */
    private static final class WindowsMapping implements Mapping {
        private final com.sun.jna.platform.win32.WinNT.HANDLE handle;
        private final com.sun.jna.Pointer address;
        private final ByteBuffer buffer;
        private WindowsMapping(com.sun.jna.platform.win32.WinNT.HANDLE handle, com.sun.jna.Pointer address) {
            this.handle = handle; this.address = address;
            buffer = address.getByteBuffer(0, HEADER_BYTES + CAPACITY);
        }
        static Mapping open(String name) {
            String version=com.sun.jna.Native.class.getPackage().getImplementationVersion();
            if (!("5.14.0".equals(version)||"5.14.0 (b0)".equals(version)) || com.sun.jna.Native.POINTER_SIZE != 8) return null;
            var kernel = com.sun.jna.platform.win32.Kernel32.INSTANCE;
            var handle = kernel.OpenFileMapping(0x0002 | 0x0004, false, name);
            if (handle == null) return null;
            var address = kernel.MapViewOfFile(handle, 0x0002 | 0x0004, 0, 0, HEADER_BYTES + CAPACITY);
            if (address == null) { kernel.CloseHandle(handle); return null; }
            try { return new WindowsMapping(handle, address); }
            catch (RuntimeException | Error e) { kernel.UnmapViewOfFile(address); kernel.CloseHandle(handle); throw e; }
        }
        @Override public ByteBuffer buffer() { return buffer; }
        @Override public void close() {
            com.sun.jna.platform.win32.Kernel32.INSTANCE.UnmapViewOfFile(address);
            com.sun.jna.platform.win32.Kernel32.INSTANCE.CloseHandle(handle);
        }
    }
}
