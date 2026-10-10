#include "precompiled.h"
#include "map_visibility.h"
#include "goldcraft/entity_visibility.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace goldcraft::engine_map {
namespace {
template <class T> int index_of(const T *base, int count, const void *pointer) {
    const auto first = reinterpret_cast<std::uintptr_t>(base);
    const auto value = reinterpret_cast<std::uintptr_t>(pointer);
    if (!base || !pointer || count < 0 || value < first || (value - first) % sizeof(T))
        return -1;
    const auto index = (value - first) / sizeof(T);
    return index < static_cast<std::uintptr_t>(count) ? static_cast<int>(index) : -1;
}
std::shared_ptr<const visibility::Source> capture(model_t *world) {
    if (!world || world != g_psv.worldmodel || !world->nodes || !world->planes || !world->leafs ||
        world->numleafs < 1 || world->numleafs >= MAX_MAP_LEAFS || world->numnodes < 1 ||
        world->numnodes > 1'048'576 || world->numplanes < 1 || world->numplanes > 1'048'576)
        throw std::invalid_argument("Invalid engine visibility source");
    constexpr visibility::Limits limits;
    const auto leaves = static_cast<std::size_t>(world->numleafs), words = (leaves + 63) / 64;
    if (leaves > limits.words / words)
        throw std::length_error("Engine visibility source budget");
    auto result = std::make_shared<visibility::Source>();
    result->planes.reserve(world->numplanes);
    for (int i = 0; i < world->numplanes; ++i) {
        const auto &plane = world->planes[i];
        result->planes.push_back({{plane.normal[0], plane.normal[1], plane.normal[2]}, plane.dist});
    }
    result->pvs.resize(leaves * words);
    result->contents.reserve(leaves + 1);
    static_assert(std::endian::native == std::endian::little);
    for (std::size_t i = 0; i <= leaves; ++i) {
        result->contents.push_back(world->leafs[i].contents);
        if (i)
            std::memcpy(result->pvs.data() + (i - 1) * words, Mod_LeafPVS(world->leafs + i, world),
                        (leaves + 7) / 8);
    }
    std::vector<int> mapping(world->numnodes, -1);
    std::vector<unsigned char> visiting(world->numnodes);
    const auto copy = [&](auto &&self, int index, int depth) -> int {
        if (depth > 256 || visiting[index])
            throw std::invalid_argument("Engine visibility BSP cycle/depth");
        if (mapping[index] >= 0)
            return mapping[index];
        visiting[index] = 1;
        const auto &node = world->nodes[index];
        const auto plane = index_of(world->planes, world->numplanes, node.plane);
        if (plane < 0)
            throw std::invalid_argument("Engine visibility plane identity");
        const auto mapped = static_cast<int>(result->nodes.size());
        mapping[index] = mapped;
        result->nodes.push_back({static_cast<std::uint32_t>(plane), {}});
        for (int side = 0; side < 2; ++side) {
            const auto leaf = index_of(world->leafs, world->numleafs + 1, node.children[side]);
            if (leaf >= 0)
                result->nodes[mapped].children[side] = -1 - leaf;
            else {
                const auto child = index_of(world->nodes, world->numnodes, node.children[side]);
                if (child < 0)
                    throw std::invalid_argument("Engine visibility child identity");
                const auto target = self(self, child, depth + 1);
                result->nodes[mapped].children[side] = target;
            }
        }
        visiting[index] = 0;
        return mapped;
    };
    result->root = copy(copy, 0, 1);
    return result;
}
} // namespace
struct Visibility {
    model_t *world;
    std::shared_ptr<const visibility::Source> source;
    visibility::EntityCache cache;
    struct Query {
        const unsigned char *mask = nullptr;
        int bytes = 0;
    };
    std::array<Query, 2> queries{};
    Visibility(model_t *model, std::shared_ptr<const visibility::Source> captured,
               std::span<const carving::Box> cuts)
        : world(model), source(std::move(captured)), cache(source, cuts) {}
};
namespace {
VisibilityCandidate active;
}
VisibilityCandidate prepare_visibility(model_t *world, std::span<const carving::Box> cuts) {
    if (cuts.empty())
        return {};
    auto source = active && active->world == world ? active->source : capture(world);
    return std::make_shared<Visibility>(world, std::move(source), cuts);
}
void commit_visibility(VisibilityCandidate candidate) noexcept { active = std::move(candidate); }
} // namespace goldcraft::engine_map

void GoldCraft_MapBeginVisibility(const float *eye, unsigned char *mask, int bytes,
                                  bool audible) noexcept {
    using namespace goldcraft::engine_map;
    if (!active)
        return;
    auto &query = active->queries[audible];
    query = {};
    if (active->world != g_psv.worldmodel || !eye || !mask || bytes <= 0 || (audible && !gPAS))
        return;
    active->cache.begin(audible);
    query = {mask, bytes};
    // A sealed excavation can have no original empty leaf at all.
    const auto *leaf = Mod_PointInLeaf(const_cast<float *>(eye), active->world);
    if (leaf && leaf->contents == CONTENTS_SOLID)
        active->cache.merge(active->cache.cavity_key({eye[0], eye[1], eye[2]}),
                            {mask, static_cast<std::size_t>(bytes)}, audible);
}
bool GoldCraft_MapMergeVisibility(const mleaf_t *leaf, unsigned char *mask, int bytes,
                                  bool audible) noexcept {
    using namespace goldcraft::engine_map;
    if (!active || active->world != g_psv.worldmodel || active->queries[audible].mask != mask ||
        active->queries[audible].bytes != bytes || bytes <= 0)
        return false;
    const auto index = index_of(active->world->leafs, active->world->numleafs + 1, leaf);
    return index > 0 && active->cache.merge(static_cast<std::uint32_t>(index),
                                            {mask, static_cast<std::size_t>(bytes)}, audible);
}
bool GoldCraft_MapEntityVisible(const edict_t *entity, const unsigned char *mask) noexcept {
    using namespace goldcraft::engine_map;
    if (!active || active->world != g_psv.worldmodel || !mask || !entity || entity->free ||
        (entity->v.flags & FL_KILLME))
        return false;
    const bool audible = active->queries[1].mask == mask;
    const auto &query = active->queries[audible];
    if (query.mask != mask || query.bytes <= 0 ||
        !active->cache.matches({mask, static_cast<std::size_t>(query.bytes)}, audible))
        return false;
    try {
        // The original leaf list omits new empty volume inside the solid leaf.
        // Preserve a plugin's custom/copied/changed PVS instead of overriding it.
        return active->cache.sees_bounds(
            {{entity->v.absmin[0], entity->v.absmin[1], entity->v.absmin[2]},
             {entity->v.absmax[0], entity->v.absmax[1], entity->v.absmax[2]}},
            audible);
    } catch (...) {
        return false;
    }
}

int GoldCraft_MapPointInterest(int sourceLeaf, const float *source, const float *receiver,
                               bool audible) noexcept {
    using namespace goldcraft::engine_map;
    if (!active || active->world != g_psv.worldmodel || !source || !receiver ||
        (audible ? !gPAS : !gPVS) || sourceLeaf < 0 || sourceLeaf > active->world->numleafs)
        return -1;
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(source[axis]) || !std::isfinite(receiver[axis]))
            return -1;
    const auto from = active->cache.key(static_cast<std::uint32_t>(sourceLeaf),
                                        {source[0], source[1], source[2]});
    if (!from)
        return -1;
    const auto *leaf = Mod_PointInLeaf(const_cast<float *>(receiver), active->world);
    const auto index = index_of(active->world->leafs, active->world->numleafs + 1, leaf);
    if (index < 0)
        return -1;
    const auto to = active->cache.key(static_cast<std::uint32_t>(index),
                                      {receiver[0], receiver[1], receiver[2]});
    return active->cache.reaches(from, to, audible) ? 1 : 0;
}
