package dev.goldcraft.bridge;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.HexFormat;
import java.util.UUID;

public final class Wire {
    private Wire() {}
    public static final int MAGIC = 0x31464347, VERSION = 30, HEADER_BYTES = 32;
    public static final int MAX_PAYLOAD = 32 * 1024 * 1024, MAX_QUEUED = 64 * 1024 * 1024;
    public static final float UNITS_PER_BLOCK = 32.0f, Y_OFFSET = 64.0f;
    public static final int HOST_CLIENT = 1, FABRIC_CLIENT = 2, HOST_SERVER = 3, FABRIC_SERVER = 4;
    public static final int FLUID_VOLUMES=79, FLUID_CAPABILITY=32;
    public static final int STATUS_EFFECTS = 8, STATUS_HUD = 76;
    public static final int STATUS_ATTRIBUTES_CAP = 16, STATUS_ATTRIBUTES = 77, STATUS_SPEND = 78;
    public static final int GAMEPLAY_RULES_CAP = 128, GAMEPLAY_RULES = 82;
    public static final int HELLO = 1, WELCOME = 2, HEARTBEAT = 3;
    public static final int LOCAL_QPC_V1 = 2, CLOCK_OFFER = 70, CLOCK_ACK = 71, CLOCK_READY = 72, TYPE_COUNT = 91;
    public static final int HOST_EXPLOSIONS_CAP = 256, HOST_EXPLOSION = 83, HOST_MATERIALS = 84;
    public static final int TNT_DAMAGE_CAP = 512;
    public static final int SKILL_GUARD_CAP = 32768;
    public static final int STATUS_VISION_CAP = 16384, STATUS_VISION = 90;
    public static final int STATUS_VISIBILITY_CAP = 8192, STATUS_VISIBILITY = 89;
    public static final int STATUS_MOTION_CAP = 4096, STATUS_MOTION = 88, STATUS_ENVIRONMENT_CAP = 2048, STATUS_ENVIRONMENT = 87;
    public static final int PLAYER_GAMEPLAY_CAP = 1024, PLAYER_GAMEPLAY = 85, PLAYER_ATTACK = 86;
    public static final int EFFECT_POSITION_HOLD = 4, EFFECT_GRANT = 73, EFFECT_POSE = 74, EFFECT_REOPEN = 75;
    public static final int SHARED_BULK_V1 = 1, BULK_OFFER = 67, BULK_ACK = 68, BULK_REF = 69;
    public static final int WORLD = 10, ACTORS = 11, CLIENT_BINDING = 12, PAIR_PLAYER = 13, PAIR_RESULT = 14;
    public static final int BSP = 15, BRUSHES = 16;
    public static final int INPUT = 20, PLAYER_POSE = 21, ATLAS = 22, SECTION_MESH = 23, REMOVE_SECTION = 24;
    public static final int DAMAGE_REQUEST = 30, DAMAGE_RESULT = 31;
    public static final int CONTROL = 25, LIGHTS = 26, ENTITY_TEXTURE = 27, ENTITY_MESH = 28;
    public static final int SCENE_RESET = 29, ATLAS_PATCHES = 32;
    public static final int HUD_FRAME = 33, VIEWPORT = 34, UI_INPUT = 35;
    public static final int PARTICLE_TEXTURE = 36, PARTICLE_MESH = 37, POST_EFFECT_PLAN = 64, PARTICLE_TEXTURE_CHUNK = 65, ENTITY_FRAME = 66;
    public static final int BLOCK_FEEDBACK = 38, CAMERA = 39, KEY_INPUT = 55;
    public static final int TRACE_QUERY = 40, TRACE_RESULT = 41, AUTHORITATIVE_POSE = 50;
    public static final int MINECRAFT_OBJECTS = 51, OBJECT_ACTION = 52, HOST_ENTITIES = 53, VITALS_DELTA = 54;
    public static final int MAP_MINING_POLICY = 56, MAP_MINING_REQUEST = 57, MAP_MINING_RESULT = 58;
    public static final int MAP_EDIT_SNAPSHOT = 59, MAP_EDIT_DELTA = 60, MAP_EDIT_QUERY = 61;
    public static final int MAP_MINING_SAMPLE = 62, MAP_MINING_SURFACE = 63;

    public record Header(int type, int size, long epoch, long sequence) {}
    public record Message(int type, byte[] payload) {}

    public static boolean knownType(int type) {
        return switch (type) {
            case STATUS_VISION, STATUS_VISIBILITY, STATUS_MOTION, STATUS_ENVIRONMENT, PLAYER_GAMEPLAY, PLAYER_ATTACK, GAMEPLAY_RULES, NativeArmorProtocol.REQUEST, NativeArmorProtocol.RESULT, FLUID_VOLUMES, STATUS_ATTRIBUTES, STATUS_SPEND, STATUS_HUD, HELLO, WELCOME, HEARTBEAT, WORLD, ACTORS, CLIENT_BINDING, PAIR_PLAYER, PAIR_RESULT, BLOCK_FEEDBACK, CAMERA, KEY_INPUT, PARTICLE_TEXTURE_CHUNK, ENTITY_FRAME,
                 INPUT, PLAYER_POSE, ATLAS, SECTION_MESH, REMOVE_SECTION, DAMAGE_REQUEST, DAMAGE_RESULT, HOST_EXPLOSION, HOST_MATERIALS,
                 BSP, BRUSHES, CONTROL, LIGHTS, ENTITY_TEXTURE, ENTITY_MESH, SCENE_RESET, ATLAS_PATCHES, HUD_FRAME, VIEWPORT, UI_INPUT, PARTICLE_TEXTURE, PARTICLE_MESH, POST_EFFECT_PLAN, TRACE_QUERY, TRACE_RESULT, AUTHORITATIVE_POSE, MINECRAFT_OBJECTS, OBJECT_ACTION, HOST_ENTITIES, VITALS_DELTA, MAP_MINING_POLICY, MAP_MINING_REQUEST, MAP_MINING_RESULT, MAP_EDIT_SNAPSHOT, MAP_EDIT_DELTA, MAP_EDIT_QUERY, MAP_MINING_SAMPLE, MAP_MINING_SURFACE -> true;
            case BULK_OFFER, BULK_ACK, BULK_REF, CLOCK_OFFER, CLOCK_ACK, CLOCK_READY, EFFECT_GRANT, EFFECT_POSE, EFFECT_REOPEN -> true;
            default -> false;
        };
    }

    public static byte[] key(String text) {
        if (text == null || !text.matches("[0-9a-fA-F]{32}")) throw new IllegalArgumentException("Expected 32 hexadecimal characters");
        return HexFormat.of().parseHex(text);
    }

    public static boolean internalType(int type) {
        return type == BULK_OFFER || type == BULK_ACK || type == BULK_REF
            || type == CLOCK_OFFER || type == CLOCK_ACK || type == CLOCK_READY;
    }

    public static boolean bulkType(int type) {
        return switch (type) {
            case ATLAS, SECTION_MESH, ENTITY_TEXTURE, ATLAS_PATCHES, HUD_FRAME,
                PARTICLE_TEXTURE, PARTICLE_MESH, POST_EFFECT_PLAN, PARTICLE_TEXTURE_CHUNK, ENTITY_FRAME -> true;
            default -> false;
        };
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

    public static ByteBuffer frameHeader(int type, long epoch, long sequence, int size) {
        if (!knownType(type) || size < 0 || size > MAX_PAYLOAD || sequence == 0) throw new IllegalArgumentException("Invalid outbound frame");
        return ByteBuffer.allocate(HEADER_BYTES).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(MAGIC).putShort((short) VERSION).putShort((short) type).putInt(size)
            .putInt(0).putLong(epoch).putLong(sequence).flip();
    }

    public static final class Writer implements AutoCloseable {
        private final ByteArrayOutputStream out;
        private byte[] destination;
        private final int offset,capacity;
        private final Thread owner;
        private int position;
        public Writer() { this(32); }
        public Writer(int initialCapacity) {
            out = new ByteArrayOutputStream(initialCapacity);offset=capacity=0;owner=null;
        }
        private Writer(byte[] destination,int offset,int length) {
            java.util.Objects.checkFromIndexSize(offset,length,destination.length);
            out=null;this.destination=destination;this.offset=offset;capacity=length;owner=Thread.currentThread();
        }
        /** A synchronous region writer; finish or close permanently revokes access. */
        public static Writer bounded(byte[] destination,int offset,int length) { return new Writer(destination,offset,length); }
        private void requireSpace(int count) {
            if(Thread.currentThread()!=owner)throw new IllegalStateException("Bounded writer thread");
            if(destination==null)throw new IllegalStateException("Closed bounded writer");
            if(count>capacity-position)throw new IllegalArgumentException("Bounded writer overflow");
        }
        private Writer integer(long value,int count) {
            if(out!=null){for(int i=0;i<count;i++)out.write((int)(value >>> (8*i)));}
            else {
                requireSpace(count);
                for(int i=0;i<count;i++)destination[offset+position++]=(byte)(value >>> (8*i));
            }
            return this;
        }
        public Writer u8(int value) { return integer(value,1); }
        public Writer u16(int value) { return integer(value,2); }
        public Writer i32(int value) { return integer(value,4); }
        public Writer i64(long value) { return integer(value,8); }
        public Writer f32(float value) { if (!Float.isFinite(value)) throw new IllegalArgumentException("Non-finite float"); return i32(Float.floatToRawIntBits(value)); }
        public Writer bytes(byte[] value) { return bytes(value,0,value.length); }
        public Writer bytes(byte[] value,int offset,int length) {
            if(out!=null)out.write(value,offset,length);
            else {
                java.util.Objects.checkFromIndexSize(offset,length,value.length);requireSpace(length);
                System.arraycopy(value,offset,destination,this.offset+position,length);position+=length;
            }
            return this;
        }
        public Writer string(String value) {
            byte[] bytes=value.getBytes(StandardCharsets.UTF_8);
            if (bytes.length>65535) throw new IllegalArgumentException("String length");
            if(out==null)requireSpace(2+bytes.length);
            return u16(bytes.length).bytes(bytes);
        }
        public int size() { return out!=null?out.size():position; }
        public byte[] toByteArray() {
            if(out!=null)return out.toByteArray();
            requireSpace(0);return java.util.Arrays.copyOfRange(destination,offset,offset+position);
        }
        /** Require the complete declared region, and revoke even an incomplete writer. */
        public void finish() {
            if(out!=null)return;
            requireSpace(0);boolean complete=position==capacity;close();
            if(!complete)throw new IllegalArgumentException("Incomplete bounded writer");
        }
        @Override public void close() {
            if(out==null){
                if(Thread.currentThread()!=owner)throw new IllegalStateException("Bounded writer thread");
                destination=null;
            }
        }
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
