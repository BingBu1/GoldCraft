#pragma once
#include "goldcraft/world_carving.hpp"
#include <bit>

namespace goldcraft::carving {
// Owned engine input, independent of client/server ABI. A match is exact;
// identities and map sessions are checked separately by each caller.
struct CollisionSource {
    struct Hull {
        std::vector<Plane> planes;
        std::vector<HullNode> nodes;
        int root = 0;
        HullView view() const { return {planes, nodes, root}; }
    };
    std::array<Hull, 4> hulls;
    std::array<Box, 4> bodies{};
    std::vector<Box> cuts;

    bool same(const CollisionSource& other) const {
        const auto scalar = [](double a, double b) {
            return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
        };
        const auto point = [&](const Point& a, const Point& b) {
            for (int axis = 0; axis < 3; ++axis) if (!scalar(a[axis], b[axis])) return false;
            return true;
        };
        const auto box = [&](const Box& a, const Box& b) { return point(a.min,b.min)&&point(a.max,b.max); };
        if (cuts.size() != other.cuts.size()) return false;
        for (std::size_t i = 0; i < cuts.size(); ++i) if (!box(cuts[i], other.cuts[i])) return false;
        for (std::size_t h = 0; h < hulls.size(); ++h) {
            const auto &a = hulls[h], &b = other.hulls[h];
            if (!box(bodies[h], other.bodies[h]) || a.root != b.root ||
                a.planes.size() != b.planes.size() || a.nodes.size() != b.nodes.size()) return false;
            for (std::size_t i = 0; i < a.planes.size(); ++i)
                if (!point(a.planes[i].normal,b.planes[i].normal) ||
                    !scalar(a.planes[i].distance,b.planes[i].distance)) return false;
            for (std::size_t i = 0; i < a.nodes.size(); ++i)
                if (a.nodes[i].plane != b.nodes[i].plane || a.nodes[i].children != b.nodes[i].children) return false;
        }
        return true;
    }
    std::size_t retained_bytes() const {
        std::size_t bytes = sizeof(CollisionSource) + 64 + cuts.capacity() * sizeof(Box);
        for (const auto& hull : hulls)
            bytes += hull.planes.capacity() * sizeof(Plane) + hull.nodes.capacity() * sizeof(HullNode);
        return bytes; // Callers cap engine arrays at 262144 and cut count at 65536.
    }
};
} // namespace goldcraft::carving
