#pragma once

#include <array>
#include <cstddef>
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

} // namespace goldcraft::carving
