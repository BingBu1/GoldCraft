#pragma once
#include "goldcraft/world_carving.hpp"
#include <memory>

namespace goldcraft::visibility {
using Bits = std::vector<std::uint64_t>;
// Unlike a collision hull, negative children encode -(leaf index + 1), not
// contents. Leaf zero is the shared solid leaf. PVS bit i denotes leaf i+1.
struct Source {
    std::vector<carving::Plane> planes;
    std::vector<carving::HullNode> nodes;
    std::int32_t root = 0;
    std::vector<std::int32_t> contents;
    // Dense, word-padded rows for leaves 1..N; no row for the shared solid leaf.
    Bits pvs;
};
struct Limits {
    std::size_t boxes = 65'536, words = 8'388'608, operations = 67'108'864;
};
struct View {
    Bits leaves, components;
    std::size_t operations = 0;
    bool sees_leaf(std::uint32_t leaf) const noexcept;
};

// Immutable per-revision topology, prepared off the draw path. Conservative
// BSP/AABB traversal collects possible contacts, including coplanar boundaries;
// it can overestimate oblique cells, but must not hide a newly opened sightline.
// Only reached edit components expand the original PVS. Original PVS alone is
// NOT transitively closed. Sealed cavities have their own virtual view keys.
// Per-leaf View/indirect-command caches belong to the same resource generation.
class Prepared {
public:
    Prepared(std::shared_ptr<const Source> source, std::span<const carving::Box> cuts,
             Limits limits = {});
    View view(std::uint32_t key) const;
    // Allocation-free camera lookup; key 0 retains the engine's no-vis view.
    std::uint32_t key(std::uint32_t original_leaf, carving::Point eye) const noexcept;
    // Preparation-time conservative selection of cavity draw bounds.
    bool sees_bounds(const View &view, const carving::Box &bounds) const;
    std::size_t leaf_count() const noexcept { return source_->contents.size(); }
    std::size_t component_count() const noexcept { return components_.size(); }
    std::size_t operations() const noexcept { return operations_; }
private:
    struct Node {
        carving::Box bounds;
        std::uint32_t first = 0, count = 0, left = 0, right = 0;
    };
    struct Component { Bits pvs; };
    std::shared_ptr<const Source> source_;
    Limits limits_;
    std::size_t words_ = 0, operations_ = 0;
    std::vector<carving::Box> cuts_;
    std::vector<Node> tree_;
    std::vector<std::uint32_t> order_, cut_components_;
    std::vector<Component> components_;
    std::vector<std::vector<std::uint32_t>> leaf_components_;
};
} // namespace goldcraft::visibility
