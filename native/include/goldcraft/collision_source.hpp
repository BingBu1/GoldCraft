#pragma once
#include "goldcraft/world_carving.hpp"
#include <bit>
#include <memory>

namespace goldcraft::carving {
// Owned engine input, independent of client/server ABI. A match is exact;
// identities and map sessions are checked separately by each caller.
struct CollisionGeometry {
    struct Hull {
        std::vector<Plane> planes;
        std::vector<HullNode> nodes;
        int root = 0;
        HullView view() const { return {planes, nodes, root}; }
    };
    std::array<Hull, 4> hulls;
    std::array<Box, 4> bodies{};
    static bool same_scalar(double a, double b) {
        return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
    }
    static bool same_point(const Point& a, const Point& b) {
        for (int axis = 0; axis < 3; ++axis) if (!same_scalar(a[axis], b[axis])) return false;
        return true;
    }
    static bool same_box(const Box& a, const Box& b) {
        return same_point(a.min,b.min) && same_point(a.max,b.max);
    }
    template <class NativePlane> static Plane copy_plane(const NativePlane& input) {
        Plane plane{{}, input.dist};
        if (input.type < 3) plane.normal[input.type] = 1;
        else for (int axis = 0; axis < 3; ++axis) plane.normal[axis] = input.normal[axis];
        return plane;
    }
    template <class NativeNode> static HullNode copy_node(const NativeNode& input) {
        return {static_cast<std::uint32_t>(input.planenum), {input.children[0], input.children[1]}};
    }
    // Caller first validates its actual engine's counts/pointers and identity.
    // Convert exactly as capture does; do not compare native padding or retain
    // native pointers. All four hulls, including unreachable entries, matter.
    template <class NativeHull> bool matches_native(int plane_count, const NativeHull* native) const {
        for (std::size_t h = 0; h < hulls.size(); ++h) {
            const auto& a = hulls[h]; const auto& b = native[h];
            const auto node_count = b.lastclipnode < 0 ? 0u : static_cast<std::size_t>(b.lastclipnode) + 1;
            if (a.root != b.firstclipnode || a.planes.size() != static_cast<std::size_t>(plane_count) ||
                a.nodes.size() != node_count) return false;
            Box body{};
            if (h) for (int axis = 0; axis < 3; ++axis) {
                body.min[axis] = b.clip_mins[axis]; body.max[axis] = b.clip_maxs[axis];
            }
            if (!same_box(bodies[h], body)) return false;
            for (std::size_t i = 0; i < a.planes.size(); ++i) {
                const auto plane = copy_plane(b.planes[i]);
                if (!same_point(a.planes[i].normal, plane.normal) ||
                    !same_scalar(a.planes[i].distance, plane.distance)) return false;
            }
            for (std::size_t i = 0; i < a.nodes.size(); ++i) {
                const auto node = copy_node(b.clipnodes[i]);
                if (a.nodes[i].plane != node.plane || a.nodes[i].children != node.children) return false;
            }
        }
        return true;
    }
    bool same(const CollisionGeometry& other) const {
        for (std::size_t h = 0; h < hulls.size(); ++h) {
            const auto &a = hulls[h], &b = other.hulls[h];
            if (!same_box(bodies[h], other.bodies[h]) || a.root != b.root ||
                a.planes.size() != b.planes.size() || a.nodes.size() != b.nodes.size()) return false;
            for (std::size_t i = 0; i < a.planes.size(); ++i)
                if (!same_point(a.planes[i].normal,b.planes[i].normal) ||
                    !same_scalar(a.planes[i].distance,b.planes[i].distance)) return false;
            for (std::size_t i = 0; i < a.nodes.size(); ++i)
                if (a.nodes[i].plane != b.nodes[i].plane || a.nodes[i].children != b.nodes[i].children) return false;
        }
        return true;
    }
    std::size_t retained_bytes() const {
        std::size_t bytes = sizeof(CollisionGeometry) + 64;
        for (const auto& hull : hulls)
            bytes += hull.planes.capacity() * sizeof(Plane) + hull.nodes.capacity() * sizeof(HullNode);
        return bytes; // Callers cap engine arrays at 262144.
    }
};
struct CollisionSource {
    std::shared_ptr<const CollisionGeometry> geometry;
    std::vector<Box> cuts;
    bool same(const CollisionSource& other) const {
        if (cuts.size() != other.cuts.size()) return false;
        for (std::size_t i = 0; i < cuts.size(); ++i)
            if (!CollisionGeometry::same_box(cuts[i], other.cuts[i])) return false;
        return geometry == other.geometry || (geometry && other.geometry && geometry->same(*other.geometry));
    }
    std::size_t retained_bytes() const {
        // Charge shared geometry in full for each target, never undercount the
        // existing per-candidate cache limit due to shared ownership.
        return sizeof(CollisionSource) + 64 + cuts.capacity() * sizeof(Box) +
            (geometry ? geometry->retained_bytes() : 0);
    }
};
} // namespace goldcraft::carving
