package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

/** Parser/lifetime boundary fixture only. No actual Windows mapping or TCP peer. */
public final class LocalQpcProofProbe {
    private static int checks;
    private static void check(boolean value) {
        ++checks;
        if (!value) throw new AssertionError("Clock proof check " + checks);
    }
    private static void rejects(Runnable action) {
        boolean rejected = false;
        try { action.run(); } catch (IllegalArgumentException expected) { rejected = true; }
        check(rejected);
    }
    private static final class Fixture {
        long counter = 1000, frequency = 1000, step;
        int reads, opens, closes;
        boolean available = true;
        final ByteBuffer mapping = ByteBuffer.allocateDirect(80).order(ByteOrder.LITTLE_ENDIAN);
        final byte[] challenge = new byte[16];
        final LocalQpcClock clock = new LocalQpcClock(raw -> {
            ++reads; raw.counter = counter; raw.frequency = frequency; counter += step; return true;
        });
        Fixture() {
            Arrays.fill(challenge, (byte) 7);
            mapping.putInt(0, LocalQpcProof.MAGIC).putInt(4, 1).putInt(8, 80).putInt(12, 0)
                .putLong(16, 42).putLong(24, 1000).putLong(32, 1000).putLong(40, 4000);
            mapping.put(48, challenge);
        }
        byte[] offer() {
            return new Wire.Writer().i32(1).i32(80).i64(42).i64(1000).i64(1000).i64(4000)
                .string("Local\\GoldCraftClock-0123456789abcdef0123456789abcdef").toByteArray();
        }
        LocalQpcProof.Response respond(byte[] offer, long epoch) {
            return LocalQpcProof.respond(offer, epoch, clock, name -> {
                ++opens;
                if (!available) return null;
                return new LocalQpcProof.Mapping() {
                    public ByteBuffer buffer() { return mapping.asReadOnlyBuffer(); }
                    public void close() { ++closes; }
                };
            });
        }
        LocalQpcProof.Response respond() { return respond(offer(), 42); }
        void decline(LocalQpcProof.Response response) {
            check(!response.accepted() && response.frequency() == 0);
            var r = new Wire.Reader(response.ack());
            check(r.i32() == 1 && r.i32() == 0 && r.i64() == 42 && r.i64() == 0);
            check(Arrays.equals(r.bytes(16), new byte[16])); r.finish();
        }
    }
    private static void valid_and_owned() {
        Fixture f = new Fixture();
        var response = f.respond();
        check(response.accepted() && response.frequency() == 1000);
        check(f.opens == 1 && f.closes == 1 && f.reads == 2);
        byte[] bytes = response.ack(); check(bytes.length == 40);
        var r = new Wire.Reader(bytes);
        check(r.i32() == 1 && r.i32() == 1 && r.i64() == 42 && r.i64() == 1000);
        check(Arrays.equals(r.bytes(16), f.challenge)); r.finish();
        Arrays.fill(bytes, (byte) 0); f.mapping.put(48, new byte[16]);
        check(response.ack()[4] == 1 && response.ack()[24] == 7); // Owned, neither caller nor map alias.
    }
    private static void invalid_offer() {
        for (int mode = 0; mode < 12; ++mode) {
            Fixture f = new Fixture(); byte[] bytes = f.offer();
            var b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
            switch (mode) {
                case 0 -> b.putInt(0, 2);
                case 1 -> b.putInt(4, 81);
                case 2 -> b.putLong(8, 43);
                case 3 -> b.putLong(16, 0);
                case 4 -> b.putLong(24, -1);
                case 5 -> b.putLong(32, 1000);
                case 6 -> b.putLong(32, 4001);
                case 7 -> bytes[42] = 'X';
                case 8 -> bytes[bytes.length - 1] = 'g';
                case 9 -> bytes = Arrays.copyOf(bytes, bytes.length - 1);
                case 10 -> bytes = Arrays.copyOf(bytes, bytes.length + 1);
                case 11 -> b.putShort(40, (short) 0);
            }
            final byte[] input = bytes;
            rejects(() -> f.respond(input, 42));
            check(f.reads == 0 && f.opens == 0 && f.closes == 0);
        }
    }
    private static void invalid_mapping() {
        for (int offset : new int[]{0, 4, 8, 12, 16, 24, 32, 40, 64, 79}) {
            Fixture f = new Fixture(); f.mapping.put(offset, (byte) (f.mapping.get(offset) ^ 1));
            rejects(f::respond);
            check(f.opens == 1 && f.closes == 1);
        }
        Fixture zero = new Fixture(); zero.mapping.put(48, new byte[16]);
        rejects(zero::respond); check(zero.closes == 1);
    }
    private static void clock_and_unavailable() {
        Fixture noMap = new Fixture(); noMap.available = false;
        noMap.decline(noMap.respond()); check(noMap.opens == 1 && noMap.closes == 0);
        for (long counter : new long[]{999, 4000}) {
            Fixture f = new Fixture(); f.counter = counter; f.decline(f.respond()); check(f.opens == 0);
        }
        Fixture delayed = new Fixture(); delayed.step = 3000;
        delayed.decline(delayed.respond()); check(delayed.opens == 1 && delayed.closes == 1 && delayed.reads == 2);
        Fixture wrongFrequency = new Fixture(); wrongFrequency.frequency = 2000;
        wrongFrequency.decline(wrongFrequency.respond());
        check(wrongFrequency.clock.failure() == LocalQpcClock.Failure.NONE && wrongFrequency.opens == 0);
        Fixture regression = new Fixture(); regression.step = -1;
        regression.decline(regression.respond());
        check(regression.clock.failure() == LocalQpcClock.Failure.COUNTER_REGRESSED && regression.closes == 1);
        check(!LocalQpcProof.supported(Wire.FABRIC_CLIENT, Wire.HOST_CLIENT));
        check(!LocalQpcProof.supported(Wire.FABRIC_SERVER, Wire.HOST_CLIENT));
    }
    public static void main(String[] arguments) {
        valid_and_owned(); invalid_offer(); invalid_mapping(); clock_and_unavailable();
        System.out.println(checks + " local QPC parser checks passed; no native mapping/TCP/FML evidence");
    }
}
