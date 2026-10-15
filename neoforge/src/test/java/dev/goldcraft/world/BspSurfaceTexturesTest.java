package dev.goldcraft.world;

import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import static org.junit.jupiter.api.Assertions.*;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

class BspSurfaceTexturesTest {
    private static BspMap.Vec v(float x, float y, float z) { return new BspMap.Vec(x, y, z); }

    @Test
    void followsWorldNodeAndReturnsFirstFaceTexture() {
        BspSurfaceTextures textures = new BspSurfaceTextures(fixture(false));
        assertEquals("STONE", textures.texture(v(0, 0, 10), v(0, 0, -10)));
        assertNull(textures.texture(v(0, 0, 10), v(0, 0, 20)), "same empty leaf has no surface");
        assertNull(textures.texture(v(40, 0, 10), v(40, 0, -10)), "outside face extents has no surface");
        assertEquals(1, textures.textureNames().size());
        assertEquals("STONE", textures.textureNames().get(0));
        assertThrows(UnsupportedOperationException.class, () -> textures.textureNames().add("MUTATED"));
    }

    @Test
    void missingTextureAndSolidOnlyPathReturnNull() {
        BspSurfaceTextures textures = new BspSurfaceTextures(fixture(true));
        assertNull(textures.texture(v(0, 0, 10), v(0, 0, -10)));
        assertNull(textures.texture(v(0, 0, -10), v(0, 0, -20)), "solid leaf has no crossed surface");
        assertTrue(textures.textureNames().isEmpty());
    }

    @Test
    void missingMiptexSlotsDoNotShiftLaterTextureReferences() {
        byte[] original = fixture(false);
        ByteBuffer source = ByteBuffer.wrap(original).order(ByteOrder.LITTLE_ENDIAN);
        int textureOffset = source.getInt(4 + 2 * 8);
        int textureEnd = textureOffset + source.getInt(8 + 2 * 8);
        byte[] bytes = new byte[original.length + 4];
        System.arraycopy(original, 0, bytes, 0, textureOffset);
        System.arraycopy(original, textureOffset + 8, bytes, textureOffset + 12, 40);
        System.arraycopy(original, textureEnd, bytes, textureEnd + 4, original.length - textureEnd);
        ByteBuffer data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        data.putInt(textureOffset, 2).putInt(textureOffset + 4, -1).putInt(textureOffset + 8, 12);
        data.putInt(8 + 2 * 8, 52);
        for (int lump = 0; lump < 15; lump++) {
            int field = 4 + lump * 8;
            int offset = data.getInt(field);
            if (offset >= textureEnd) data.putInt(field, offset + 4);
        }
        int texInfoOffset = data.getInt(4 + 6 * 8);
        data.putInt(texInfoOffset + 32, 1);
        BspSurfaceTextures textures = new BspSurfaceTextures(bytes);
        assertEquals("STONE", textures.texture(v(0, 0, 10), v(0, 0, -10)));
        assertEquals(java.util.List.of("STONE"), textures.textureNames());
    }

    @Test
    void usesRehldsTurbulentSurfaceExtents() {
        byte[] bytes = fixture(false);
        ByteBuffer data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        int textureOffset = data.getInt(4 + 2 * 8) + 8;
        byte[] name = "!WATER".getBytes(StandardCharsets.ISO_8859_1);
        System.arraycopy(name, 0, bytes, textureOffset, name.length);
        BspSurfaceTextures textures = new BspSurfaceTextures(bytes);
        assertEquals("!WATER", textures.texture(v(40, 0, 10), v(40, 0, -10)));
        assertNull(textures.texture(v(9000, 0, 10), v(9000, 0, -10)));
    }

    @Test
    void rejectsInvalidHeaderGraphAndSurfaceIndices() {
        int[][] edits = {
                {4 + 3 * 8, Integer.MAX_VALUE}, // vertex lump outside file
                {8 + 3 * 8, 47}, // vertex record size
                {4 + 13 * 8, -1}, // negative surfedge lump offset
        };
        for (int[] edit : edits) {
            byte[] bytes = fixture(false);
            ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(edit[0], edit[1]);
            assertThrows(IllegalArgumentException.class, () -> new BspSurfaceTextures(bytes));
        }
        byte[] cycle = fixture(false);
        ByteBuffer data = ByteBuffer.wrap(cycle).order(ByteOrder.LITTLE_ENDIAN);
        data.putShort(data.getInt(4 + 5 * 8) + 4, (short) 0);
        assertThrows(IllegalArgumentException.class, () -> new BspSurfaceTextures(cycle));
        byte[] badEdge = fixture(false);
        data = ByteBuffer.wrap(badEdge).order(ByteOrder.LITTLE_ENDIAN);
        data.putInt(data.getInt(4 + 13 * 8), Integer.MIN_VALUE);
        assertThrows(IllegalArgumentException.class, () -> new BspSurfaceTextures(badEdge));
    }

    @Test
    void rejectsOutOfBoundsLumpReference() {
        byte[] bytes = fixture(false);
        ByteBuffer header = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        // The nodes lump starts at header slot 5 (offset field at 4 + 5*8).
        int nodeOffset = header.getInt(4 + 5 * 8);
        ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).putInt(nodeOffset + 20, 99);
        assertThrows(IllegalArgumentException.class, () -> new BspSurfaceTextures(bytes));
    }

    @Test
    void readsRealAssaultTextureTableWhenSandboxMapIsPresent() throws Exception {
        Path map = Path.of("../sandbox/cs-client-a/Half-Life/cstrike/maps/cs_assault.bsp");
        assumeTrue(Files.exists(map));
        BspSurfaceTextures textures = new BspSurfaceTextures(Files.readAllBytes(map));
        assertFalse(textures.textureNames().isEmpty());
        assertTrue(textures.textureNames().stream().allMatch(name -> name != null && !name.isEmpty()));
    }

    @Test
    void rejectsWeakMaterialWhenALaterDirectionExhaustsBudget() {
        BspSurfaceTextures textures = new BspSurfaceTextures(wallFixture());
        var budget = new BspSurfaceTextures.Budget(5);
        int[] sampled = {0};
        int selected = HostExplosions.material(textures, v(-10, 0, 0),
                java.util.Map.of("STONE", dev.goldcraft.bridge.MapMining.WOOD), budget,
                material -> { sampled[0]++; return 3.0; });
        assertEquals(1, sampled[0], "the weak material must be read before exhaustion");
        assertTrue(budget.exhausted(), "a later direction must exhaust this cell's budget");
        assertEquals(dev.goldcraft.bridge.MapMining.UNBREAKABLE, selected,
                "an incomplete cell must not retain the earlier weak material");
        assertNull(textures.texture(v(-10, 0, 0), v(10, 0, 0), budget));
        assertTrue(budget.exhausted(), "later queries cannot revive an exhausted budget");
    }

    @Test
    void keepsWeakMaterialWhenAllDirectionsFitBudget() {
        BspSurfaceTextures textures = new BspSurfaceTextures(wallFixture());
        var budget = new BspSurfaceTextures.Budget(13);
        int[] sampled = {0};
        int selected = HostExplosions.material(textures, v(-10, 0, 0),
                java.util.Map.of("STONE", dev.goldcraft.bridge.MapMining.WOOD), budget,
                material -> { sampled[0]++; return 3.0; });
        assertEquals(1, sampled[0]);
        assertEquals(dev.goldcraft.bridge.MapMining.WOOD, selected);
        assertFalse(budget.exhausted());
    }

    // Rotate the existing horizontal face into x=0, with the solid leaf at
    // negative x. Ray -X costs two steps; +X finds the weak face in three;
    // the following -Y ray is where a five-step shared budget runs out.
    private static byte[] wallFixture() {
        byte[] bytes = fixture(false);
        ByteBuffer data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        int plane = data.getInt(4 + 1 * 8);
        data.putFloat(plane, 1).putFloat(plane + 8, 0).putInt(plane + 16, 0);
        int vertices = data.getInt(4 + 3 * 8);
        for (int i = 0; i < 4; i++) {
            int at = vertices + i * 12;
            float x = data.getFloat(at), y = data.getFloat(at + 4);
            data.putFloat(at, 0).putFloat(at + 4, x).putFloat(at + 8, y);
        }
        int tex = data.getInt(4 + 6 * 8);
        data.putFloat(tex, 0).putFloat(tex + 4, 1);
        data.putFloat(tex + 20, 0).putFloat(tex + 24, 1);
        return bytes;
    }

    private static byte[] fixture(boolean missingTexture) {
        byte[][] lumps = new byte[15][];
        lumps[1] = bytes(20, b -> {
            b.putFloat(0).putFloat(0).putFloat(1).putFloat(0).putInt(2);
        });
        lumps[2] = bytes(48, b -> {
            b.putInt(1).putInt(missingTexture ? -1 : 8);
            b.put("STONE".getBytes(StandardCharsets.ISO_8859_1));
            b.put(new byte[11]); // fixed 16-byte name
            b.putInt(64).putInt(64).putInt(40).putInt(0).putInt(0).putInt(0);
        });
        lumps[3] = bytes(48, b -> {
            float[][] points = {{-32, -32, 0}, {32, -32, 0}, {32, 32, 0}, {-32, 32, 0}};
            for (float[] point : points) b.putFloat(point[0]).putFloat(point[1]).putFloat(point[2]);
        });
        lumps[5] = bytes(24, b -> {
            b.putInt(0).putShort((short) -1).putShort((short) -2);
            b.putShort((short) -32).putShort((short) -32).putShort((short) -1);
            b.putShort((short) 32).putShort((short) 32).putShort((short) 1);
            b.putShort((short) 0).putShort((short) 1);
        });
        lumps[6] = bytes(40, b -> {
            b.putFloat(1).putFloat(0).putFloat(0).putFloat(0);
            b.putFloat(0).putFloat(1).putFloat(0).putFloat(0);
            b.putInt(0).putInt(0);
        });
        lumps[7] = bytes(20, b -> {
            b.putShort((short) 0).putShort((short) 0).putInt(0).putShort((short) 4);
            b.putShort((short) 0).put(new byte[8]);
        });
        lumps[10] = bytes(56, b -> {
            b.putInt(-1).put(new byte[24]); // empty leaf
            b.putInt(-2).put(new byte[24]); // solid leaf
        });
        lumps[12] = bytes(16, b -> {
            b.putShort((short) 0).putShort((short) 1);
            b.putShort((short) 1).putShort((short) 2);
            b.putShort((short) 2).putShort((short) 3);
            b.putShort((short) 3).putShort((short) 0);
        });
        lumps[13] = bytes(16, b -> b.putInt(0).putInt(1).putInt(2).putInt(3));
        lumps[14] = bytes(64, b -> {
            for (int i = 0; i < 3; i++) b.putFloat(-32);
            for (int i = 0; i < 3; i++) b.putFloat(32);
            for (int i = 0; i < 3; i++) b.putFloat(0);
            b.putInt(0).putInt(-1).putInt(-1).putInt(-1).putInt(1).putInt(0).putInt(1);
        });

        int total = 124;
        int[] offsets = new int[15];
        for (int i = 0; i < lumps.length; i++) {
            if (lumps[i] != null) {
                offsets[i] = total;
                total += lumps[i].length;
            }
        }
        ByteBuffer output = ByteBuffer.allocate(total).order(ByteOrder.LITTLE_ENDIAN);
        output.putInt(30);
        for (int i = 0; i < lumps.length; i++) {
            output.putInt(offsets[i]);
            output.putInt(lumps[i] == null ? 0 : lumps[i].length);
        }
        for (int i = 0; i < lumps.length; i++) {
            if (lumps[i] != null) output.position(offsets[i]).put(lumps[i]);
        }
        return output.array();
    }

    @FunctionalInterface
    private interface Writer { void write(ByteBuffer buffer); }

    private static byte[] bytes(int size, Writer writer) {
        ByteBuffer buffer = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN);
        writer.write(buffer);
        if (buffer.position() != size) throw new AssertionError("fixture size mismatch");
        return buffer.array();
    }
}
