package dev.goldcraft.world;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Reads the world model's render surfaces from a classic GoldSrc BSP30 file.
 *
 * <p>This is the data equivalent of ReHLDS {@code TraceTexture}/{@code
 * SurfaceAtPoint}.  All lump references are checked once by the constructor;
 * a query then follows the BSP node path and tests only the surfaces attached
 * to nodes crossed by that segment.  The class deliberately does not infer a
 * material from a texture prefix.</p>
 */
public final class BspSurfaceTextures {
    private static final int LUMP_COUNT = 15;
    private static final int LUMP_PLANES = 1;
    private static final int LUMP_TEXTURES = 2;
    private static final int LUMP_VERTICES = 3;
    private static final int LUMP_NODES = 5;
    private static final int LUMP_TEXINFO = 6;
    private static final int LUMP_FACES = 7;
    private static final int LUMP_EDGES = 12;
    private static final int LUMP_SURFEDGES = 13;
    private static final int LUMP_MODELS = 14;
    private static final int MAX_TEXTURES = 4096;
    private static final int MAX_NODES = 32767;
    private static final int MAX_FACES = 65535;
    private static final int MAX_VERTICES = 65535;
    private static final int MAX_EDGES = 256 * 1024;
    private static final int MAX_SURFEDGES = 256 * 1024;
    private static final int MAX_TEXINFO = 65535;
    private static final float MAX_COORDINATE = 1_000_000.0f;
    private static final int MAX_EDGE_REFERENCES = 1_048_576;
    public static final class Budget {
        private int remaining;
        public Budget(int operations) { remaining=operations; }
        private void step() { if (--remaining < 0) throw new WorkLimit(); }
    }
    private static final class WorkLimit extends RuntimeException {
        private WorkLimit() { super(null,null,false,false); }
    }

    private record Lump(int offset, int length) {}
    private record Plane(BspMap.Vec normal, float distance) {}
    private record Node(int plane, int front, int back, int firstFace, int numFaces) {}
    private record Texture(String name, int width, int height) {}
    private record TexInfo(float[] s, float[] t, int miptex, int flags) {}
    private record Surface(TexInfo texInfo,
                           int textureMinS, int textureMinT, int extentS, int extentT) {}
    private record Model(int headNode, int firstFace, int numFaces) {}

    private final Plane[] planes;
    private final Node[] nodes;
    private final Surface[] surfaces;
    private final Model world;
    /** Texture names indexed by miptex number; null denotes a missing miptex. */
    private final Texture[] textureTable;
    private final List<String> textureNames;

    /**
     * Parses and validates one complete BSP30 image.  The byte array is
     * consumed during construction and is never retained, so ray queries do
     * not re-read the map.
     */
    public BspSurfaceTextures(byte[] bytes) {
        if (bytes == null) throw invalid("null file");
        if (bytes.length < 124 || bytes.length > BspMap.MAX_BYTES) throw invalid("file size");
        ByteBuffer data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if (data.getInt() != 30) throw invalid("only BSP30 is supported");
        Lump[] lumps = readLumps(data, bytes.length);

        this.planes = readPlanes(data, lumps[LUMP_PLANES]);
        BspMap.Vec[] vertices = readVertices(data, lumps[LUMP_VERTICES]);
        this.textureTable = readTextures(data, bytes, lumps[LUMP_TEXTURES]);
        List<String> listedNames = new ArrayList<>();
        for (Texture texture : textureTable) if (texture != null) listedNames.add(texture.name);
        this.textureNames = Collections.unmodifiableList(listedNames);
        TexInfo[] texInfos = readTexInfos(data, lumps[LUMP_TEXINFO], textureTable.length);
        int[] edgeStart = new int[count(lumps[LUMP_EDGES], 4, MAX_EDGES, "edges")];
        int[] edgeEnd = new int[edgeStart.length];
        readEdges(data, lumps[LUMP_EDGES], vertices.length, edgeStart, edgeEnd);
        int[] surfaceEdges = readSurfEdges(data, lumps[LUMP_SURFEDGES], edgeStart.length);
        this.surfaces = readFaces(data, lumps[LUMP_FACES], planes.length, texInfos,
                vertices, edgeStart, edgeEnd, surfaceEdges, textureTable);
        this.nodes = readNodes(data, lumps[LUMP_NODES], planes.length, surfaces.length, lumps[10]);
        this.world = readWorldModel(data, lumps[LUMP_MODELS], nodes.length, surfaces.length);
        validateTree(nodes);
    }

    /** Returns the texture table in BSP order, omitting missing/empty miptex entries. */
    public List<String> textureNames() {
        return textureNames;
    }

    /**
     * Returns the first world-model surface crossed by the segment, or null
     * for a segment that never reaches a mapped face, a missing texture, or a
     * pure-solid/internal leaf.
     */
    public String texture(BspMap.Vec from, BspMap.Vec to) {
        return texture(from,to,new Budget(16384));
    }
    public String texture(BspMap.Vec from, BspMap.Vec to, Budget budget) {
        if (from == null || to == null) throw invalid("null query point");
        for (int axis = 0; axis < 3; axis++) {
            if (Math.abs(from.axis(axis)) > MAX_COORDINATE || Math.abs(to.axis(axis)) > MAX_COORDINATE) return null;
        }
        if (world.headNode < 0) return null;
        final int surface;
        try { surface=surfaceAtPoint(world.headNode,from,to,budget); }
        catch (WorkLimit exhausted) { return null; }
        return surface < 0 ? null : textureName(surfaces[surface].texInfo.miptex);
    }

    private int surfaceAtPoint(int nodeIndex, BspMap.Vec start, BspMap.Vec end, Budget budget) {
        budget.step();
        if (nodeIndex < 0) return -1;
        Node node = nodes[nodeIndex];
        Plane plane = planes[node.plane];
        float front = signedDistance(plane, start);
        float back = signedDistance(plane, end);
        int near = front < 0.0f ? 1 : 0;
        int far = back < 0.0f ? 1 : 0;
        if (near == far) return surfaceAtPoint(child(node, near), start, end,budget);

        float fraction = front / (front - back);
        // The signs differ, therefore the denominator is non-zero.  Keep the
        // guard for malformed-but-finite arithmetic at very large coordinates.
        if (!Float.isFinite(fraction)) return -1;
        BspMap.Vec middle = interpolate(start, end, fraction);
        int hit = surfaceAtPoint(child(node, near), start, middle,budget);
        if (hit >= 0) return hit;

        for (int i = 0; i < node.numFaces; i++) {
            budget.step();
            int surfaceIndex = node.firstFace + i;
            if (surfaceIndex < world.firstFace ||
                    surfaceIndex >= world.firstFace + world.numFaces) continue;
            Surface surface = surfaces[surfaceIndex];
            TexInfo tex = surface.texInfo;
            float s = dotTex(tex.s, middle), t = dotTex(tex.t, middle);
            if (!Float.isFinite(s) || !Float.isFinite(t)) return -1;
            long ds = (int) s, dt = (int) t;
            if (ds < surface.textureMinS || dt < surface.textureMinT) continue;
            if (ds - surface.textureMinS > surface.extentS ||
                    dt - surface.textureMinT > surface.extentT) continue;
            // A missing miptex is an unknown surface.  Do not guess a material
            // and do not continue to a farther surface on the same crossing.
            return surfaceIndex;
        }
        return surfaceAtPoint(child(node, far), middle, end,budget);
    }

    private String textureName(int index) {
        if (index < 0 || index >= textureTable.length) return null;
        Texture texture = textureTable[index];
        return texture == null ? null : texture.name;
    }

    private static int child(Node node, int side) {
        return side == 0 ? node.front : node.back;
    }

    private static float signedDistance(Plane plane, BspMap.Vec point) {
        return plane.normal.dot(point) - plane.distance;
    }

    private static BspMap.Vec interpolate(BspMap.Vec a, BspMap.Vec b, float fraction) {
        return new BspMap.Vec(
                a.x() + (b.x() - a.x()) * fraction,
                a.y() + (b.y() - a.y()) * fraction,
                a.z() + (b.z() - a.z()) * fraction);
    }

    private static float dotTex(float[] tex, BspMap.Vec point) {
        return point.x() * tex[0] + point.y() * tex[1] + point.z() * tex[2] + tex[3];
    }

    private static float extentTex(float[] tex, BspMap.Vec point) {
        // CalcSurfaceExtents in the pinned ReHLDS computes in double and
        // stores the result in float before floor/ceil to the 16-unit grid.
        return (float) (point.x() * (double) tex[0] + point.y() * (double) tex[1] +
                point.z() * (double) tex[2] + tex[3]);
    }

    private static Lump[] readLumps(ByteBuffer data, int fileLength) {
        Lump[] lumps = new Lump[LUMP_COUNT];
        for (int i = 0; i < LUMP_COUNT; i++) {
            int offset = data.getInt();
            int length = data.getInt();
            if (offset < 0 || length < 0 || (long) offset + length > fileLength) {
                throw invalid("lump range " + i);
            }
            if (length > 0 && offset < 124) throw invalid("lump header overlap " + i);
            for (int j = 0; j < i; j++) {
                Lump previous = lumps[j];
                if (length > 0 && previous.length > 0 &&
                        offset < (long) previous.offset + previous.length &&
                        previous.offset < (long) offset + length) {
                    throw invalid("overlapping lumps");
                }
            }
            lumps[i] = new Lump(offset, length);
        }
        // These are required to prove that the image describes a world model.
        for (int index : new int[]{LUMP_PLANES, LUMP_VERTICES, LUMP_NODES,
                LUMP_TEXINFO, LUMP_FACES, LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS}) {
            if (lumps[index].length == 0) throw invalid("missing lump " + index);
        }
        return lumps;
    }

    private static Plane[] readPlanes(ByteBuffer data, Lump lump) {
        int count = count(lump, 20, 65535, "planes");
        Plane[] result = new Plane[count];
        for (int i = 0; i < count; i++) {
            int p = lump.offset + i * 20;
            BspMap.Vec normal = new BspMap.Vec(data.getFloat(p), data.getFloat(p + 4), data.getFloat(p + 8));
            float distance = data.getFloat(p + 12);
            int type = data.getInt(p + 16);
            if (!Float.isFinite(distance) || Math.abs(distance) > MAX_COORDINATE ||
                    type < 0 || type > 5 || Math.abs(normal.dot(normal) - 1.0f) > 0.02f) {
                throw invalid("plane " + i);
            }
            if (type < 3 && Math.abs(Math.abs(normal.axis(type)) - 1.0f) > 0.001f) {
                throw invalid("axial plane " + i);
            }
            result[i] = new Plane(normal, distance);
        }
        return result;
    }

    private static BspMap.Vec[] readVertices(ByteBuffer data, Lump lump) {
        int count = count(lump, 12, MAX_VERTICES, "vertices");
        BspMap.Vec[] result = new BspMap.Vec[count];
        for (int i = 0; i < count; i++) {
            int p = lump.offset + i * 12;
            BspMap.Vec vertex = new BspMap.Vec(data.getFloat(p), data.getFloat(p + 4), data.getFloat(p + 8));
            if (Math.abs(vertex.x()) > MAX_COORDINATE || Math.abs(vertex.y()) > MAX_COORDINATE ||
                    Math.abs(vertex.z()) > MAX_COORDINATE) throw invalid("vertex " + i);
            result[i] = vertex;
        }
        return result;
    }

    private static Texture[] readTextures(ByteBuffer data, byte[] bytes, Lump lump) {
        if (lump.length == 0) return new Texture[0];
        if (lump.length < 4) throw invalid("texture lump size");
        int count = data.getInt(lump.offset);
        if (count < 0 || count > MAX_TEXTURES || 4L + count * 4L > lump.length) {
            throw invalid("texture count");
        }
        Texture[] textures = new Texture[count];
        for (int i = 0; i < count; i++) {
            int relative = data.getInt(lump.offset + 4 + i * 4);
            if (relative == -1) continue;
            if (relative < 4L + count * 4L || (long) relative + 40 > lump.length) throw invalid("texture offset " + i);
            int p = lump.offset + relative;
            int end = p;
            while (end < p + 16 && bytes[end] != 0) end++;
            if (end == p) continue;
            String name = new String(bytes, p, end - p, StandardCharsets.ISO_8859_1);
            // A miptex name is a fixed 16-byte field.  Keep only names that
            // contain no control character; material interpretation remains
            // the caller's responsibility.
            boolean valid = true;
            for (int n = 0; n < name.length(); n++) {
                if (name.charAt(n) < 0x20 || name.charAt(n) == 0x7f) {
                    valid = false;
                    break;
                }
            }
            int width = data.getInt(p + 16), height = data.getInt(p + 20);
            if (width <= 0 || height <= 0 || width > 65536 || height > 65536) throw invalid("texture dimensions");
            if (valid) textures[i] = new Texture(name, width, height);
        }
        return textures;
    }

    private static TexInfo[] readTexInfos(ByteBuffer data, Lump lump, int textureCount) {
        int count = count(lump, 40, MAX_TEXINFO, "texinfo");
        TexInfo[] result = new TexInfo[count];
        for (int i = 0; i < count; i++) {
            int p = lump.offset + i * 40;
            float[] s = new float[4];
            float[] t = new float[4];
            for (int j = 0; j < 4; j++) s[j] = data.getFloat(p + j * 4);
            for (int j = 0; j < 4; j++) t[j] = data.getFloat(p + 16 + j * 4);
            for (float value : s) if (!Float.isFinite(value)) throw invalid("texinfo vector");
            for (float value : t) if (!Float.isFinite(value)) throw invalid("texinfo vector");
            int miptex = data.getInt(p + 32);
            if (miptex < -1 || (textureCount > 0 && miptex >= textureCount)) throw invalid("texinfo texture index");
            result[i] = new TexInfo(s, t, textureCount == 0 ? -1 : miptex, data.getInt(p + 36));
        }
        return result;
    }

    private static void readEdges(ByteBuffer data, Lump lump, int vertexCount,
                                  int[] starts, int[] ends) {
        for (int i = 0; i < starts.length; i++) {
            int p = lump.offset + i * 4;
            int a = Short.toUnsignedInt(data.getShort(p));
            int b = Short.toUnsignedInt(data.getShort(p + 2));
            if (a >= vertexCount || b >= vertexCount) throw invalid("edge vertex");
            starts[i] = a;
            ends[i] = b;
        }
    }

    private static int[] readSurfEdges(ByteBuffer data, Lump lump, int edgeCount) {
        int count = count(lump, 4, MAX_SURFEDGES, "surfedges");
        int[] result = new int[count];
        for (int i = 0; i < count; i++) {
            int edge = data.getInt(lump.offset + i * 4);
            if (edge == Integer.MIN_VALUE || Math.abs(edge) >= edgeCount) throw invalid("surfedge reference");
            result[i] = edge;
        }
        return result;
    }

    private static Surface[] readFaces(ByteBuffer data, Lump lump, int planeCount,
                                       TexInfo[] texInfos, BspMap.Vec[] vertices,
                                       int[] edgeStart, int[] edgeEnd, int[] surfaceEdges, Texture[] textures) {
        int count = count(lump, 20, MAX_FACES, "faces");
        Surface[] result = new Surface[count];
        long references=0;
        for (int i = 0; i < count; i++) {
            int p = lump.offset + i * 20;
            int plane = Short.toUnsignedInt(data.getShort(p));
            int firstEdge = data.getInt(p + 4);
            int numEdges = Short.toUnsignedInt(data.getShort(p + 8));
            references+=numEdges;
            if (references>MAX_EDGE_REFERENCES) throw invalid("aggregate face edge budget");
            int texInfo = Short.toUnsignedInt(data.getShort(p + 10));
            if (plane >= planeCount || texInfo >= texInfos.length || firstEdge < 0 || numEdges < 3 ||
                    (long) firstEdge + numEdges > surfaceEdges.length) {
                throw invalid("face " + i);
            }
            float[] min = {Float.POSITIVE_INFINITY, Float.POSITIVE_INFINITY};
            float[] max = {Float.NEGATIVE_INFINITY, Float.NEGATIVE_INFINITY};
            TexInfo info = texInfos[texInfo];
            for (int edgeIndex = 0; edgeIndex < numEdges; edgeIndex++) {
                int surfEdge = surfaceEdges[firstEdge + edgeIndex];
                if (surfEdge == Integer.MIN_VALUE) throw invalid("surfedge value");
                int edge = surfEdge < 0 ? -surfEdge : surfEdge;
                if (edge < 0 || edge >= edgeStart.length) throw invalid("face edge");
                int vertex = surfEdge >= 0 ? edgeStart[edge] : edgeEnd[edge];
                BspMap.Vec point = vertices[vertex];
                float s = extentTex(info.s, point), t = extentTex(info.t, point);
                if (!Float.isFinite(s) || !Float.isFinite(t)) throw invalid("face texture coordinates");
                min[0] = Math.min(min[0], s);
                min[1] = Math.min(min[1], t);
                max[0] = Math.max(max[0], s);
                max[1] = Math.max(max[1], t);
            }
            int minS = textureGrid(min[0], false), minT = textureGrid(min[1], false);
            int extentS = textureGrid(max[0], true) - minS;
            int extentT = textureGrid(max[1], true) - minT;
            Texture texture = info.miptex < 0 ? null : textures[info.miptex];
            if (texture != null && !texture.name.startsWith("sky")) {
                String name = texture.name;
                if (name.startsWith("scroll")) {
                    extentS = texture.width;
                    extentT = texture.height;
                } else if (name.startsWith("!") || name.regionMatches(true, 0, "laser", 0, 5) ||
                        name.regionMatches(true, 0, "water", 0, 5)) {
                    minS = minT = -8192;
                    extentS = extentT = 16384;
                } else if ((info.flags & 1) != 0) {
                    extentS = texture.width;
                    extentT = texture.height;
                }
            }
            result[i] = new Surface(info, minS, minT, extentS, extentT);
        }
        return result;
    }

    private static int textureGrid(float value, boolean ceil) {
        double grid = ceil ? Math.ceil(value / 16.0) : Math.floor(value / 16.0);
        if (Math.abs(grid) > 16_777_215) throw invalid("texture extent range");
        return (int) grid * 16;
    }

    private static Node[] readNodes(ByteBuffer data, Lump lump, int planeCount,
                                    int surfaceCount, Lump leavesLump) {
        int leafCount = count(leavesLump, 28, 32767, "leaves");
        int[] contents = new int[leafCount];
        for (int i = 0; i < leafCount; i++) {
            int value = data.getInt(leavesLump.offset + i * 28);
            if (value > -1 || value < -15) throw invalid("leaf contents");
            contents[i] = value;
        }
        int count = count(lump, 24, MAX_NODES, "nodes");
        Node[] result = new Node[count];
        for (int i = 0; i < count; i++) {
            int p = lump.offset + i * 24;
            int plane = data.getInt(p);
            int front = decodeChild(data.getShort(p + 4), count, contents.length);
            int back = decodeChild(data.getShort(p + 6), count, contents.length);
            int firstFace = Short.toUnsignedInt(data.getShort(p + 20));
            int numFaces = Short.toUnsignedInt(data.getShort(p + 22));
            if (plane < 0 || plane >= planeCount || (long) firstFace + numFaces > surfaceCount) {
                throw invalid("node " + i);
            }
            result[i] = new Node(plane, front, back, firstFace, numFaces);
        }
        return result;
    }

    private static int decodeChild(short encoded, int nodeCount, int leafCount) {
        int value = encoded;
        if (value >= 0) {
            if (value >= nodeCount) throw invalid("node child");
            return value;
        }
        int leaf = -1 - value;
        if (leaf < 0 || leaf >= leafCount) throw invalid("node leaf child");
        return value;
    }

    private static Model readWorldModel(ByteBuffer data, Lump lump, int nodeCount, int surfaceCount) {
        int count = count(lump, 64, 4096, "models");
        if (count == 0) throw invalid("missing world model");
        int p = lump.offset;
        int head = data.getInt(p + 36);
        int firstFace = data.getInt(p + 56);
        int numFaces = data.getInt(p + 60);
        if (head < 0 || head >= nodeCount || firstFace < 0 || numFaces < 0 ||
                (long) firstFace + numFaces > surfaceCount) throw invalid("world model");
        return new Model(head, firstFace, numFaces);
    }

    private static void validateTree(Node[] nodes) {
        byte[] state = new byte[nodes.length];
        int[] heights = new int[nodes.length];
        for (int i = 0; i < nodes.length; i++) validateBranch(nodes, state, heights, i, 0);
    }

    private static int validateBranch(Node[] nodes, byte[] state, int[] heights, int index, int depth) {
        if (index < 0) return 0;
        if (depth > 256) throw invalid("node tree depth");
        if (state[index] == 1) throw invalid("cyclic node tree");
        if (state[index] == 2) return heights[index];
        state[index] = 1;
        int height = 1 + Math.max(validateBranch(nodes, state, heights, nodes[index].front, depth + 1),
                validateBranch(nodes, state, heights, nodes[index].back, depth + 1));
        if (height > 256) throw invalid("node tree depth");
        heights[index] = height;
        state[index] = 2;
        return height;
    }

    private static int count(Lump lump, int stride, int limit, String name) {
        if (lump.length % stride != 0) throw invalid("record size " + name);
        int count = lump.length / stride;
        if (count > limit) throw invalid("record count " + name);
        return count;
    }

    private static IllegalArgumentException invalid(String message) {
        return new IllegalArgumentException("BSP textures: " + message);
    }
}
