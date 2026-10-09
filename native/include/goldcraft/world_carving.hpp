#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace goldcraft::carving {

// GoldSrc world coordinates. A renderer can interpolate every original vertex
// attribute (including both texture coordinates) using the retained weights.
using Point = std::array<double, 3>;
using Triangle = std::array<Point, 3>;
struct Vertex {
    Point position;
    std::array<double, 3> weights;
};
using Polygon = std::vector<Vertex>;
struct Box { Point min, max; };
struct Limits {
    std::size_t fragments = 4096;
    std::size_t operations = 262144;
};
struct Result {
    std::vector<Polygon> pieces;
    std::size_t operations = 0;
    bool changed = false;
};

// Subtract the union of closed boxes from a surface triangle. Output pieces
// are convex, retain the input winding, and do not overlap in their interiors.
// Coplanar faces on a box boundary belong to the removed volume. No epsilon
// grows the requested cavity. Empty input area produces no surface geometry.
//
// Invalid input throws invalid_argument. Excessive work/fragmentation throws
// length_error, without returning a partial mesh. This only constructs surface
// geometry: it does not change engine hulls, PVS, map state, or authorization.
Result subtract(const Triangle& triangle, std::span<const Box> boxes, Limits limits = {});

// A point-contents BSP in model-local coordinates, independent of engine ABI.
// children[0] is the normal-facing side; negative children are GoldSrc contents.
// Callers translate render leaves to contents before supplying this view.
struct Plane { Point normal; double distance; };
struct HullNode { std::uint32_t plane; std::array<std::int32_t, 2> children; };
struct HullView {
    std::span<const Plane> planes;
    std::span<const HullNode> nodes;
    std::int32_t root;
};
// Validate finite unit planes and the reachable acyclic graph. Returns work
// consumed, so a caller can include validation in its total operation budget.
std::size_t validate_hull(HullView hull, Limits limits = {});
struct Wall {
    std::vector<Point> vertices;
    Point normal; // Points from remaining solid into the excavated cavity.
    std::size_t box;
};
struct Interior {
    std::vector<Wall> walls;
    std::size_t operations = 0;
};

// Construct the boundary of the cut-box union that faces remaining SOLID BSP
// volume. Clip exactly against its point hull, preserve openings into air/water,
// remove shared faces, and emit coincident exterior faces once. Coplanar BSP
// boundaries use contents immediately outside the cavity. Output polygons are
// convex and wound toward the cavity; no original texture/material is implied.
// Invalid/cyclic/deeper-than-256 hulls fail; limits cover the whole operation.
// This supplies missing interior surfaces, not player hulls or runtime state.
Interior interior_walls(HullView hull, std::span<const Box> boxes, Limits limits = {});

} // namespace goldcraft::carving
