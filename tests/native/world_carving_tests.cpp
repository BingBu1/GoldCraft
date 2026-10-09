#include "goldcraft/world_carving.hpp"
#include "goldcraft/wire.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

using namespace goldcraft::carving;

namespace {
Point minus(const Point& a, const Point& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Point cross(const Point& a, const Point& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Point& a, const Point& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double area(const Triangle& triangle) {
    auto n = cross(minus(triangle[1], triangle[0]), minus(triangle[2], triangle[0]));
    return std::sqrt(dot(n, n)) * 0.5;
}
double area(const Result& result) {
    double total = 0;
    for (const auto& polygon : result.pieces)
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
            total += area(Triangle{polygon[0].position, polygon[i].position, polygon[i + 1].position});
    return total;
}
void close(double a, double b, double tolerance = 1e-7) {
    if (std::abs(a - b) > tolerance) throw std::runtime_error("Geometric/attribute conservation failed");
}
template<class Exception, class Function> void rejects(Function operation) {
    bool rejected = false;
    try { operation(); } catch (const Exception&) { rejected = true; }
    assert(rejected);
}
bool inside(const Point& p, const Box& box) {
    for (int axis = 0; axis < 3; ++axis)
        if (p[axis] < box.min[axis] || p[axis] > box.max[axis]) return false;
    return true;
}

using Texinfo = std::array<std::array<double, 4>, 2>;
double uv(const Point& point, const std::array<double, 4>& vector) {
    return point[0] * vector[0] + point[1] * vector[1] + point[2] * vector[2] + vector[3];
}

struct Coverage { std::size_t checked = 0, boundary = 0; };
Coverage verify(const Triangle& triangle, std::span<const Box> boxes, const Result& result,
                const Texinfo& texture = {{{1.25, -0.5, 0.75, 71}, {-0.25, 2, 0.5, -13}}}) {
    Coverage samples;
    const Point normal = cross(minus(triangle[1], triangle[0]), minus(triangle[2], triangle[0]));
    assert(area(result) <= area(triangle) + 1e-6);
    for (const auto& polygon : result.pieces) {
        assert(polygon.size() >= 3);
        for (const auto& vertex : polygon) {
            close(vertex.weights[0] + vertex.weights[1] + vertex.weights[2], 1);
            Point reconstructed{};
            for (std::size_t i = 0; i < 3; ++i) {
                assert(vertex.weights[i] >= -1e-12 && vertex.weights[i] <= 1 + 1e-12);
                for (int axis = 0; axis < 3; ++axis)
                    reconstructed[axis] += triangle[i][axis] * vertex.weights[i];
            }
            for (int axis = 0; axis < 3; ++axis) close(reconstructed[axis], vertex.position[axis]);
            for (const auto& vector : texture) {
                double mapped = 0, lightmap = 0;
                for (std::size_t i = 0; i < 3; ++i) {
                    const double source_uv = uv(triangle[i], vector);
                    mapped += vertex.weights[i] * source_uv;
                    lightmap += vertex.weights[i] * ((source_uv - 32) / 16 + 0.5);
                }
                close(mapped, uv(vertex.position, vector));
                close(lightmap, (uv(vertex.position, vector) - 32) / 16 + 0.5);
            }
        }
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            const auto face_normal = cross(minus(polygon[i].position, polygon[0].position),
                                           minus(polygon[i + 1].position, polygon[0].position));
            assert(dot(face_normal, normal) >= -1e-7);
        }
    }
    // An independent point-membership oracle checks both missing surface and
    // overlapping interiors in the source triangle's barycentric 2D plane.
    std::uint32_t seed = 0x739125ab;
    auto random = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return double(seed) / 4294967296.0;
    };
    for (unsigned sample = 0; sample < 64; ++sample) {
        double u = random(), v = random();
        if (u + v > 1) { u = 1 - u; v = 1 - v; }
        const double weights[]{1 - u - v, u, v};
        Point point{};
        for (int i = 0; i < 3; ++i)
            for (int axis = 0; axis < 3; ++axis) point[axis] += weights[i] * triangle[i][axis];
        const bool removed = std::any_of(boxes.begin(), boxes.end(), [&](const auto& box) { return inside(point, box); });
        unsigned coverage = 0;
        bool boundary = false;
        for (const auto& polygon : result.pieces) {
            bool contained = true, edge = false;
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const auto& a = polygon[i].weights;
                const auto& b = polygon[(i + 1) % polygon.size()].weights;
                const double side = (b[1] - a[1]) * (v - a[2]) - (b[2] - a[2]) * (u - a[1]);
                if (side < -1e-12) { contained = false; break; }
                edge |= std::abs(side) <= 1e-12;
            }
            if (contained) { ++coverage; boundary |= edge; }
        }
        if (boundary) ++samples.boundary;
        else { assert(coverage == (removed ? 0u : 1u)); ++samples.checked; }
    }
    return samples;
}

void unit_cases() {
    const Triangle triangle{{{0, 0, 0}, {128, 0, 0}, {0, 128, 0}}};
    const Box center{{16, 16, -8}, {48, 48, 8}};
    const auto original = subtract(triangle, {});
    assert(!original.changed && original.pieces.size() == 1);
    for (std::size_t i = 0; i < 3; ++i) assert(original.pieces[0][i].position == triangle[i]);

    auto cut = subtract(triangle, {&center, 1});
    assert(cut.changed); close(area(cut), 8192 - 1024); verify(triangle, {&center, 1}, cut);
    const std::array<Box, 2> adjacent{{{{16, 16, -8}, {32, 48, 8}}, {{32, 16, -8}, {48, 48, 8}}}};
    const auto joined = subtract(triangle, adjacent);
    close(area(joined), area(cut)); verify(triangle, adjacent, joined);
    std::array<Box, 2> overlap{{center, {{32, 32, -8}, {64, 64, 8}}}};
    auto overlapping = subtract(triangle, overlap);
    close(area(overlapping), 8192 - 1792); verify(triangle, overlap, overlapping);
    std::reverse(overlap.begin(), overlap.end());
    auto reversed = subtract(triangle, overlap);
    close(area(reversed), area(overlapping)); verify(triangle, overlap, reversed);
    const std::array<Box, 2> duplicate{center, center};
    const auto repeated = subtract(triangle, duplicate);
    close(area(repeated), area(cut)); verify(triangle, duplicate, repeated);

    const Box coplanar{{-16, -16, 0}, {144, 144, 32}};
    auto removed = subtract(triangle, {&coplanar, 1});
    assert(removed.changed && removed.pieces.empty());
    const Box tangent{{128, 0, -8}, {160, 32, 8}};
    const Box beyond{{96, 96, -8}, {128, 128, 8}};
    for (const auto& miss : {tangent, beyond}) {
        auto unchanged = subtract(triangle, {&miss, 1});
        assert(!unchanged.changed && unchanged.pieces.size() == 1);
        for (std::size_t i = 0; i < 3; ++i) assert(unchanged.pieces[0][i].position == triangle[i]);
    }

    const Triangle angled{{{-2304, 1504, -80}, {-2176, 1504, -48}, {-2304, 1632, -16}}};
    const std::array<Box, 2> holes{{{{-2290, 1514, -90}, {-2240, 1568, 0}}, {{-2256, 1550, -70}, {-2224, 1582, 0}}}};
    auto sloped = subtract(angled, holes);
    assert(sloped.changed); verify(angled, holes, sloped);
    Triangle reverse_winding = angled;
    std::swap(reverse_winding[1], reverse_winding[2]);
    auto back = subtract(reverse_winding, holes);
    close(area(sloped), area(back)); verify(reverse_winding, holes, back);
    const Triangle degenerate{{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}};
    assert(subtract(degenerate, {&center, 1}).pieces.empty());

    rejects<std::length_error>([&] { subtract(triangle, {&center, 1}, {1, 262144}); });
    rejects<std::length_error>([&] { subtract(triangle, {&center, 1}, {4096, 1}); });
    rejects<std::length_error>([&] { subtract(triangle, duplicate, {4096, 1}); });
    rejects<std::invalid_argument>([&] { subtract(triangle, {}, {0, 1}); });
    const std::array<Box, 2> invalid{{coplanar, {{5, 1, 0}, {5, 2, 3}}}};
    rejects<std::invalid_argument>([&] { subtract(triangle, invalid); });
    for (double invalid_coordinate : {std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::infinity(), 1'000'001.0}) {
        Triangle bad = triangle; bad[1][0] = invalid_coordinate;
        rejects<std::invalid_argument>([&] { subtract(bad, {}); });
    }
    std::cout << "Surface carving: area, union/overlap, boundary ownership, winding, attributes, coverage and bounded failure passed\n";
}

double wall_area(const Interior& interior) {
    double total = 0;
    for (const auto& wall : interior.walls) {
        assert(wall.vertices.size() >= 3);
        close(dot(wall.normal, wall.normal), 1);
        for (std::size_t i = 1; i + 1 < wall.vertices.size(); ++i) {
            const Triangle triangle{wall.vertices[0], wall.vertices[i], wall.vertices[i + 1]};
            assert(dot(cross(minus(triangle[1], triangle[0]), minus(triangle[2], triangle[0])), wall.normal) >= 0);
            total += area(triangle);
        }
    }
    return total;
}

int point_contents(HullView hull, const Point& point) {
    auto node = hull.root;
    while (node >= 0) {
        const auto& branch = hull.nodes[node];
        const auto& plane = hull.planes[branch.plane];
        node = branch.children[dot(plane.normal, point) < plane.distance ? 1 : 0];
    }
    return node;
}

Coverage wall_coverage(HullView hull, std::span<const Box> boxes, const Interior& interior) {
    Coverage samples;
    for (const auto& wall : interior.walls) {
        assert(wall.box < boxes.size());
        for (const auto& point : wall.vertices) assert(inside(point, boxes[wall.box]));
    }
    std::uint32_t seed = 0xa791f35b;
    auto random = [&seed] { seed = seed * 1664525u + 1013904223u; return (seed + 0.5) / 4294967296.0; };
    for (const auto& box : boxes) for (int axis = 0; axis < 3; ++axis) for (bool upper : {false, true}) {
        Point inward{}; inward[axis] = upper ? -1 : 1;
        for (int sample = 0; sample < 64; ++sample) {
            Point point{}, outside{};
            for (int a = 0; a < 3; ++a)
                point[a] = a == axis ? (upper ? box.max[a] : box.min[a]) : box.min[a] + (box.max[a] - box.min[a]) * random();
            outside = point; outside[axis] -= inward[axis] * 1e-5;
            // Independent contents/union membership at points on the material
            // side detects missing walls and duplicate faces, not just area.
            const bool expected = point_contents(hull, outside) == -2 &&
                !std::any_of(boxes.begin(), boxes.end(), [&](const auto& other) { return inside(outside, other); });
            unsigned coverage = 0;
            bool boundary = false;
            for (const auto& wall : interior.walls) {
                if (wall.normal != inward || std::abs(wall.vertices[0][axis] - point[axis]) > 1e-9) continue;
                bool contains = true, edge = false;
                for (std::size_t i = 0; i < wall.vertices.size(); ++i) {
                    const auto& a = wall.vertices[i]; const auto& b = wall.vertices[(i + 1) % wall.vertices.size()];
                    const double side = dot(cross(minus(b, a), minus(point, a)), wall.normal);
                    if (side < -1e-8) { contains = false; break; }
                    edge |= std::abs(side) <= 1e-8;
                }
                if (contains) { ++coverage; boundary |= edge; }
            }
            if (boundary) ++samples.boundary;
            else { assert(coverage == unsigned(expected)); ++samples.checked; }
        }
    }
    return samples;
}

void interior_cases() {
    const HullView solid{{}, {}, -2};
    const Box cube{{0, 0, 0}, {2, 2, 2}};
    const auto check = [&](HullView hull, std::span<const Box> boxes, double expected) {
        const auto result = interior_walls(hull, boxes);
        close(wall_area(result), expected);
        wall_coverage(hull, boxes, result);
    };
    check(solid, {}, 0);
    check(solid, {&cube, 1}, 24);
    check(HullView{{}, {}, -1}, {&cube, 1}, 0);
    check(HullView{{}, {}, -3}, {&cube, 1}, 0);
    std::array<Box, 2> pair{cube, cube};
    check(solid, pair, 24); // Coincident faces have exactly one owner.
    pair[1] = {{2, 0, 0}, {4, 2, 2}};
    check(solid, pair, 40); // Shared wall between adjacent cells disappears.
    pair[1] = {{1, 0, 0}, {3, 2, 2}};
    check(solid, pair, 32);
    std::reverse(pair.begin(), pair.end()); check(solid, pair, 32);
    pair = {cube, Box{{1, 1, 1}, {3, 3, 3}}};
    check(solid, pair, 42);
    pair = {Box{{-3, -3, -3}, {3, 3, 3}}, cube};
    check(solid, pair, 216);
    const std::array<Box, 3> corner{cube, Box{{2, 0, 0}, {4, 2, 2}}, Box{{0, 2, 0}, {2, 4, 2}}};
    check(solid, corner, 56);

    const Box crossing{{-1, -1, -1}, {1, 1, 1}}, flush{{0, -1, -1}, {1, 1, 1}}, touching{{-1, -1, -1}, {0, 1, 1}};
    std::array<Plane, 2> planes{Plane{{1, 0, 0}, 0}, Plane{{1, 0, 0}, 0.125}};
    std::array<HullNode, 2> nodes{HullNode{0, {-2, -1}}, HullNode{1, {-1, -2}}};
    check(HullView{planes, nodes, 0}, {&crossing, 1}, 12);
    check(HullView{planes, nodes, 0}, {&flush, 1}, 12);
    check(HullView{planes, nodes, 0}, {&touching, 1}, 4); // Preserve the surface removed by a coplanar cut.
    planes[0].normal = {std::sqrt(0.5), std::sqrt(0.5), 0};
    check(HullView{planes, nodes, 0}, {&crossing, 1}, 12);
    planes[0].normal = {1, 0, 0}; nodes[0].children[0] = 1;
    check(HullView{planes, nodes, 0}, {&crossing, 1}, 1);
    // The exact coplanar decision must not jump across very thin solids.
    planes[1].distance = std::ldexp(1.0, -30);
    close(wall_area(interior_walls(HullView{planes, nodes, 0}, {&crossing, 1})), 8 * planes[1].distance, 1e-15);
    nodes[0].children = {-2, -2};
    check(HullView{planes, nodes, 0}, {&crossing, 1}, 24);

    rejects<std::length_error>([&] { interior_walls(solid, {&cube, 1}, Limits{1, 4096}); });
    rejects<std::length_error>([&] { interior_walls(solid, {&cube, 1}, Limits{4096, 1}); });
    rejects<std::invalid_argument>([&] { interior_walls(solid, {&cube, 1}, Limits{0, 4096}); });
    nodes[0].children[0] = 0;
    rejects<std::invalid_argument>([&] { interior_walls(HullView{planes, nodes, 0}, {&cube, 1}); });
    nodes[0].children[0] = 5;
    rejects<std::invalid_argument>([&] { interior_walls(HullView{planes, nodes, 0}, {&cube, 1}); });
    nodes[0].children[0] = -16;
    rejects<std::invalid_argument>([&] { interior_walls(HullView{planes, nodes, 0}, {&cube, 1}); });
    nodes[0].children[0] = -2; nodes[0].plane = 2;
    rejects<std::invalid_argument>([&] { interior_walls(HullView{planes, nodes, 0}, {&cube, 1}); });
    nodes[0].plane = 0; planes[0].normal = {0, 0, 0};
    rejects<std::invalid_argument>([&] { interior_walls(HullView{planes, nodes, 0}, {&cube, 1}); });
    planes[0].normal = {1, 0, 0}; planes[0].distance = std::numeric_limits<double>::quiet_NaN();
    rejects<std::invalid_argument>([&] { interior_walls(HullView{planes, nodes, 0}, {&cube, 1}); });
    const Box invalid{{0, 0, 0}, {0, 1, 1}};
    rejects<std::invalid_argument>([&] { interior_walls(solid, {&invalid, 1}); });
    std::vector<HullNode> deep(257, HullNode{0, {-2, -1}});
    for (std::size_t i = 0; i + 1 < deep.size(); ++i) deep[i].children[0] = std::int32_t(i + 1);
    planes[0].distance = 0;
    rejects<std::invalid_argument>([&] { interior_walls(HullView{planes, deep, 0}, {&cube, 1}); });
    std::cout << "Interior walls: exact solid clipping, diagonal/thin/coplanar boundaries, union ownership, inward winding, coverage and bounded failure passed\n";
}

// File layouts follow pinned ReHLDS public/rehlds/bspfile.h, not engine memory.
// The actual-map test reads sandbox data only and never writes a BSP or engine.
struct BspFile {
    std::vector<std::uint8_t> bytes;
    std::array<std::size_t, 15> offset{}, length{};
    explicit BspFile(const char* path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Missing isolated BSP fixture");
        bytes.assign(std::istreambuf_iterator<char>(file), {});
        assert(bytes.size() >= 124 && u32(0) == 30);
        assert(goldcraft::crc32(bytes) == 0xf6725c06u);
        for (std::size_t i = 0; i < 15; ++i) {
            offset[i] = u32(4 + i * 8); length[i] = u32(8 + i * 8);
            assert(offset[i] <= bytes.size() && length[i] <= bytes.size() - offset[i]);
        }
    }
    std::uint16_t u16(std::size_t at) const {
        assert(at <= bytes.size() && bytes.size() - at >= 2);
        return std::uint16_t(bytes[at]) | (std::uint16_t(bytes[at + 1]) << 8);
    }
    std::uint32_t u32(std::size_t at) const {
        return std::uint32_t(u16(at)) | (std::uint32_t(u16(at + 2)) << 16);
    }
    double f32(std::size_t at) const { return std::bit_cast<float>(u32(at)); }
    std::size_t record(std::size_t lump, std::size_t index, std::size_t stride) const {
        assert(length[lump] % stride == 0 && index < length[lump] / stride);
        return offset[lump] + index * stride;
    }
    Point vertex(std::int32_t surfedge) const {
        const auto index = static_cast<std::size_t>(surfedge < 0 ? -std::int64_t(surfedge) : surfedge);
        const auto edge = record(12, index, 4);
        const auto at = record(3, u16(edge + (surfedge < 0 ? 2 : 0)), 12);
        return {f32(at), f32(at + 4), f32(at + 8)};
    }
};

void actual_map(const char* path) {
    const BspFile bsp(path);
    const auto model = bsp.record(14, 0, 64);
    const auto first = bsp.u32(model + 56), count = bsp.u32(model + 60);
    std::size_t triangles = 0, changed = 0, pieces = 0;
    Coverage samples;
    for (std::size_t f = first; f < first + std::size_t(count); ++f) {
        const auto face = bsp.record(7, f, 20);
        const auto first_edge = bsp.u32(face + 4);
        const auto edge_count = bsp.u16(face + 8);
        const auto tex = bsp.record(6, bsp.u16(face + 10), 40);
        Texinfo texture{};
        for (std::size_t row = 0; row < 2; ++row)
            for (std::size_t col = 0; col < 4; ++col) texture[row][col] = bsp.f32(tex + row * 16 + col * 4);
        std::vector<Point> vertices;
        for (std::size_t edge = 0; edge < edge_count; ++edge) {
            const auto surfedge = bsp.record(13, first_edge + edge, 4);
            vertices.push_back(bsp.vertex(std::bit_cast<std::int32_t>(bsp.u32(surfedge))));
        }
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i) {
            const Triangle triangle{vertices[0], vertices[i], vertices[i + 1]};
            if (area(triangle) == 0) continue;
            Box hole{};
            for (int axis = 0; axis < 3; ++axis) {
                const double center = (triangle[0][axis] + triangle[1][axis] + triangle[2][axis]) / 3;
                hole.min[axis] = std::floor(center / 32) * 32;
                hole.max[axis] = hole.min[axis] + 32;
            }
            const auto result = subtract(triangle, {&hole, 1});
            const auto coverage = verify(triangle, {&hole, 1}, result, texture);
            samples.checked += coverage.checked; samples.boundary += coverage.boundary;
            ++triangles; changed += result.changed; pieces += result.pieces.size();
        }
    }
    assert(triangles > 1000 && changed > 1000);
    std::cout << "{\"map\":\"cs_assault\",\"crc32\":\"f6725c06\",\"triangles\":" << triangles
              << ",\"changed\":" << changed << ",\"pieces\":" << pieces
              << ",\"coverageSamples\":" << samples.checked << ",\"boundarySamplesSkipped\":" << samples.boundary
              << ",\"passed\":true}\n";

    std::vector<Plane> planes;
    for (std::size_t i = 0; i < bsp.length[1] / 20; ++i) {
        const auto at = bsp.record(1, i, 20);
        planes.push_back({{bsp.f32(at), bsp.f32(at + 4), bsp.f32(at + 8)}, bsp.f32(at + 12)});
    }
    std::vector<HullNode> nodes;
    for (std::size_t i = 0; i < bsp.length[5] / 24; ++i) {
        const auto at = bsp.record(5, i, 24);
        HullNode node{bsp.u32(at), {}};
        for (int side = 0; side < 2; ++side) {
            const auto child = std::bit_cast<std::int16_t>(bsp.u16(at + 4 + side * 2));
            node.children[side] = child >= 0 ? child : std::bit_cast<std::int32_t>(bsp.u32(bsp.record(10, std::size_t(-1 - child), 28)));
        }
        nodes.push_back(node);
    }
    const HullView hull{planes, nodes, std::bit_cast<std::int32_t>(bsp.u32(model + 36))};
    std::size_t cases = 0, walls = 0, operations = 0;
    Coverage interior_samples;
    for (std::size_t f = first; f < first + std::size_t(count); f += std::max<std::size_t>(1, count / 96)) {
        const auto face = bsp.record(7, f, 20);
        const auto first_edge = bsp.u32(face + 4); const auto edge_count = bsp.u16(face + 8);
        if (edge_count < 3) continue;
        Point center{};
        for (std::size_t edge = 0; edge < edge_count; ++edge) {
            const auto point = bsp.vertex(std::bit_cast<std::int32_t>(bsp.u32(bsp.record(13, first_edge + edge, 4))));
            for (int axis = 0; axis < 3; ++axis) center[axis] += point[axis] / edge_count;
        }
        const auto& plane = planes[bsp.u16(face)];
        const double side = bsp.u16(face + 2) ? -1 : 1;
        Box hole;
        for (int axis = 0; axis < 3; ++axis) {
            hole.min[axis] = std::floor((center[axis] - side * plane.normal[axis] * 0.125) / 32) * 32;
            hole.max[axis] = hole.min[axis] + 32;
        }
        std::array<Box, 3> cuts{hole, hole, hole};
        const auto adjacent = cases % 3, overlapping = (cases + 1) % 3;
        cuts[1].min[adjacent] += 32; cuts[1].max[adjacent] += 32;
        cuts[2].min[overlapping] += 16; cuts[2].max[overlapping] += 16;
        const auto interior = interior_walls(hull, cuts);
        const auto coverage = wall_coverage(hull, cuts, interior);
        wall_area(interior); // Independently verify winding and nonnegative area.
        ++cases; walls += interior.walls.size(); operations += interior.operations;
        interior_samples.checked += coverage.checked; interior_samples.boundary += coverage.boundary;
    }
    assert(cases >= 96 && walls > 100);
    std::cout << "{\"map\":\"cs_assault\",\"interiorCases\":" << cases << ",\"walls\":" << walls
              << ",\"operations\":" << operations << ",\"coverageSamples\":" << interior_samples.checked
              << ",\"boundarySamplesSkipped\":" << interior_samples.boundary << ",\"passed\":true}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) { unit_cases(); interior_cases(); }
        else if (argc == 2) actual_map(argv[1]);
        else throw std::invalid_argument("Usage: goldcraft_world_carving_tests [isolated cs_assault.bsp]");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
