package dev.goldcraft.bridge;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.HexFormat;
import java.util.UUID;

public final class Wire {
    private Wire() {}
    public static final int MAGIC = 0x31464347, VERSION = 20, HEADER_BYTES = 32;
    public static final int MAX_PAYLOAD = 32 * 1024 * 1024, MAX_QUEUED = 64 * 1024 * 1024;
    public static final float UNITS_PER_BLOCK = 32.0f, Y_OFFSET = 64.0f;
    public static final int HOST_CLIENT = 1, FABRIC_CLIENT = 2, HOST_SERVER = 3, FABRIC_SERVER = 4;
    public static final int HELLO = 1, WELCOME = 2, HEARTBEAT = 3;
    public static final int WORLD = 10, ACTORS = 11, CLIENT_BINDING = 12, PAIR_PLAYER = 13, PAIR_RESULT = 14;
    public static final int BSP = 15, BRUSHES = 16;
    public static final int INPUT = 20, PLAYER_POSE = 21, ATLAS = 22, SECTION_MESH = 23, REMOVE_SECTION = 24;
    public static final int DAMAGE_REQUEST = 30, DAMAGE_RESULT = 31;
    public static final int CONTROL = 25, LIGHTS = 26, ENTITY_TEXTURE = 27, ENTITY_MESH = 28;
    public static final int SCENE_RESET = 29, ATLAS_PATCHES = 32;
    public static final int HUD_FRAME = 33, VIEWPORT = 34, UI_INPUT = 35;
    public static final int PARTICLE_TEXTURE = 36, PARTICLE_MESH = 37;
    public static final int BLOCK_FEEDBACK = 38, CAMERA = 39, KEY_INPUT = 55;
    public static final int TRACE_QUERY = 40, TRACE_RESULT = 41, AUTHORITATIVE_POSE = 50;
    public static final int MINECRAFT_OBJECTS = 51, OBJECT_ACTION = 52, HOST_ENTITIES = 53, VITALS_DELTA = 54;
    public static final int MAP_MINING_POLICY = 56, MAP_MINING_REQUEST = 57, MAP_MINING_RESULT = 58;
    public static final int MAP_EDIT_SNAPSHOT = 59, MAP_EDIT_DELTA = 60, MAP_EDIT_QUERY = 61;

    public record Header(int type, int size, long epoch, long sequence) {}
    public record Message(int type, byte[] payload) {}

    public static boolean knownType(int type) {
        return switch (type) {
            case HELLO, WELCOME, HEARTBEAT, WORLD, ACTORS, CLIENT_BINDING, PAIR_PLAYER, PAIR_RESULT, BLOCK_FEEDBACK, CAMERA, KEY_INPUT,
                 INPUT, PLAYER_POSE, ATLAS, SECTION_MESH, REMOVE_SECTION, DAMAGE_REQUEST, DAMAGE_RESULT,
                 BSP, BRUSHES, CONTROL, LIGHTS, ENTITY_TEXTURE, ENTITY_MESH, SCENE_RESET, ATLAS_PATCHES, HUD_FRAME, VIEWPORT, UI_INPUT, PARTICLE_TEXTURE, PARTICLE_MESH, TRACE_QUERY, TRACE_RESULT, AUTHORITATIVE_POSE, MINECRAFT_OBJECTS, OBJECT_ACTION, HOST_ENTITIES, VITALS_DELTA, MAP_MINING_POLICY, MAP_MINING_REQUEST, MAP_MINING_RESULT, MAP_EDIT_SNAPSHOT, MAP_EDIT_DELTA, MAP_EDIT_QUERY -> true;
            default -> false;
        };
    }

    public static byte[] key(String text) {
        if (text == null || !text.matches("[0-9a-fA-F]{32}")) throw new IllegalArgumentException("Expected 32 hexadecimal characters");
        return HexFormat.of().parseHex(text);
    }

    public static byte[] uuid(UUID id) {
        return ByteBuffer.allocate(16).putLong(id.getMostSignificantBits()).putLong(id.getLeastSignificantBits()).array();
    }

    public static UUID uuid(byte[] bytes) {
        if (bytes.length != 16) throw new IllegalArgumentException("UUID length");
        ByteBuffer b = ByteBuffer.wrap(bytes);
        return new UUID(b.getLong(), b.getLong());
    }

    public static Header header(ByteBuffer input) {
        if (input.remaining() != HEADER_BYTES) throw new IllegalArgumentException("Header length");
        ByteBuffer b = input.slice().order(ByteOrder.LITTLE_ENDIAN);
        if (b.getInt() != MAGIC || Short.toUnsignedInt(b.getShort()) != VERSION) throw new IllegalArgumentException("Protocol magic/version mismatch");
        int type = Short.toUnsignedInt(b.getShort()), size = b.getInt(), flags = b.getInt();
        long epoch = b.getLong(), sequence = b.getLong();
        if (!knownType(type) || size < 0 || size > MAX_PAYLOAD || flags != 0 || sequence == 0) throw new IllegalArgumentException("Invalid frame header");
        return new Header(type, size, epoch, sequence);
    }

    public static ByteBuffer frame(int type, long epoch, long sequence, byte[] payload) {
        if (!knownType(type) || payload.length > MAX_PAYLOAD || sequence == 0) throw new IllegalArgumentException("Invalid outbound frame");
        return ByteBuffer.allocate(HEADER_BYTES + payload.length).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(MAGIC).putShort((short) VERSION).putShort((short) type).putInt(payload.length)
            .putInt(0).putLong(epoch).putLong(sequence).put(payload).flip();
    }

    public static final class Writer {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream();
        public Writer u8(int value) { out.write(value & 255); return this; }
        public Writer u16(int value) { u8(value); u8(value >>> 8); return this; }
        public Writer i32(int value) { for (int i=0; i<4; i++) u8(value >>> (8*i)); return this; }
        public Writer i64(long value) { for (int i=0; i<8; i++) u8((int)(value >>> (8*i))); return this; }
        public Writer f32(float value) { if (!Float.isFinite(value)) throw new IllegalArgumentException("Non-finite float"); return i32(Float.floatToRawIntBits(value)); }
        public Writer bytes(byte[] value) { out.writeBytes(value); return this; }
        public Writer string(String value) {
            byte[] bytes=value.getBytes(StandardCharsets.UTF_8);
            if (bytes.length>65535) throw new IllegalArgumentException("String length");
            return u16(bytes.length).bytes(bytes);
        }
        public byte[] toByteArray() { return out.toByteArray(); }
    }

    public static final class Reader {
        private final ByteBuffer data;
        public Reader(byte[] bytes) { data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN); }
        public int u8() { require(1); return Byte.toUnsignedInt(data.get()); }
        public int u16() { require(2); return Short.toUnsignedInt(data.getShort()); }
        public int i32() { require(4); return data.getInt(); }
        public long i64() { require(8); return data.getLong(); }
        public float f32() { require(4); float f=data.getFloat(); if (!Float.isFinite(f)) throw new IllegalArgumentException("Non-finite float"); return f; }
        public byte[] bytes(int count) { require(count); byte[] b=new byte[count]; data.get(b); return b; }
        public String string(int limit) { int count=u16(); if (count>limit) throw new IllegalArgumentException("String limit"); return new String(bytes(count),StandardCharsets.UTF_8); }
        public int remaining() { return data.remaining(); }
        public void finish() { if (remaining()!=0) throw new IllegalArgumentException("Trailing payload"); }
        private void require(int size) { if (size<0||size>remaining()) throw new IllegalArgumentException("Truncated payload"); }
    }
}
