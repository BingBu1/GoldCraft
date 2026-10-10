#include "goldcraft/carved_visibility.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

namespace { std::atomic<std::size_t> allocations{}; }
void *operator new(std::size_t bytes) {
    ++allocations;
    if (auto *p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }

using namespace goldcraft;
namespace {
std::shared_ptr<visibility::Source> Rooms() {
    auto source = std::make_shared<visibility::Source>();
    source->contents = {-2, -1, -1, -1, -1, -1};
    source->pvs = {1, 2, 4, 8, 16};
    for (std::uint32_t i = 0; i < 8; ++i) {
        source->planes.push_back({{1, 0, 0}, double((i / 2) * 10 + (i % 2) * 2)});
        source->nodes.push_back({i, {i == 7 ? -6 : std::int32_t(i + 1), i % 2 ? -1 : -2 - std::int32_t(i / 2)}});
    }
    return source;
}
carving::Box Hole(double from, double to) { return {{from, -1, -1}, {to, 1, 1}}; }
template <class F> void Reject(F &&operation) {
    bool caught = false;
    try { operation(); } catch (const std::exception &) { caught = true; }
    assert(caught);
}
std::uint32_t Leaf(const visibility::Source &source, carving::Point point) {
    auto node = source.root;
    while (node >= 0) {
        const auto &branch = source.nodes[node];
        const auto &plane = source.planes[branch.plane];
        double d = -plane.distance;
        for (int i = 0; i < 3; ++i) d += plane.normal[i] * point[i];
        node = branch.children[d >= 0 ? 0 : 1];
    }
    return static_cast<std::uint32_t>(-1 - node);
}
void Topology() {
    const auto source = Rooms();
    const std::vector cuts{Hole(0, 2), Hole(10, 12), Hole(30, 32), Hole(20.5, 21.5)};
    const visibility::Prepared prepared(source, cuts);
    assert(prepared.component_count() == 4);
    const auto a = prepared.view(1), b = prepared.view(5);
    for (std::uint32_t leaf = 1; leaf <= 5; ++leaf) {
        assert(a.sees_leaf(leaf) == (leaf <= 3));
        assert(b.sees_leaf(leaf) == (leaf >= 4));
    }
    assert(!a.sees_leaf(0) && !a.sees_leaf(65));
    assert(prepared.sees_bounds(a, cuts[0]) && prepared.sees_bounds(a, cuts[1]));
    assert(!prepared.sees_bounds(a, cuts[2]) && !prepared.sees_bounds(a, cuts[3]));
    const auto sealed_key = prepared.key(0, {21, 0, 0});
    assert(sealed_key >= source->contents.size());
    const auto sealed = prepared.view(sealed_key);
    for (std::uint32_t leaf = 1; leaf <= 5; ++leaf) assert(!sealed.sees_leaf(leaf));
    assert(prepared.sees_bounds(sealed, cuts[3]) && !prepared.sees_bounds(sealed, cuts[0]));
    assert(prepared.key(2, {21, 0, 0}) == 2 && prepared.key(0, {21, 4, 0}) == 0);
    assert(prepared.key(0, {std::numeric_limits<double>::quiet_NaN(), 0, 0}) == 0);
    const auto novis = prepared.view(0);
    for (std::uint32_t leaf = 1; leaf <= 5; ++leaf) assert(novis.sees_leaf(leaf));
    for (auto cut : cuts) assert(prepared.sees_bounds(novis, cut));
    Reject([&] { prepared.view(10); });
    Reject([&] { prepared.sees_bounds({}, cuts[0]); });

    // Touching cuts in the solid slab form ONE virtual cavity; two separated
    // cuts cannot see through the retained material between them.
    const std::vector touching{Hole(20, 21), Hole(21, 22), Hole(21, 22)};
    const visibility::Prepared joined(source, touching);
    assert(joined.component_count() == 1);
    const auto joined_view = joined.view(joined.key(0, {21, 0, 0}));
    assert(joined_view.sees_leaf(3) && joined_view.sees_leaf(4) && !joined_view.sees_leaf(1));
    const std::vector separate{Hole(20, 20.9), Hole(21.1, 22)};
    const visibility::Prepared apart(source, separate);
    assert(apart.component_count() == 2);
    assert(!apart.view(3).sees_leaf(4));

    auto directed = Rooms();
    directed->pvs[0] = 3; directed->pvs[1] = 6;
    const visibility::Prepared unedited(directed, {});
    assert(unedited.view(1).sees_leaf(2) && !unedited.view(1).sees_leaf(3));
    directed = Rooms(); directed->pvs[2] |= 8;
    const visibility::Prepared chain(directed, cuts);
    assert(chain.view(1).sees_leaf(5)); // another edit reached through original PVS
    assert(!chain.sees_bounds(chain.view(1), cuts[3])); // sealed cavity stays hidden
    const visibility::Prepared restored(source, {});
    assert(restored.view(1).sees_leaf(1) && !restored.view(1).sees_leaf(2));
    assert(restored.key(0, {1, 0, 0}) == 0 && prepared.view(1).sees_leaf(2));
}
void ObliqueAndFailures() {
    auto source = Rooms();
    const auto n = std::sqrt(.5);
    for (auto &plane : source->planes) plane.normal = {n, n, 0};
    const std::vector cuts{carving::Box{{-.1, -.1, -1}, {1.5, 1.5, 1}}};
    const visibility::Prepared prepared(source, cuts);
    const auto view = prepared.view(prepared.key(0, {.5, .5, 0}));
    for (int i = 0; i <= 100; ++i) for (int j = 0; j <= 100; ++j) {
        const auto leaf = Leaf(*source, {-.1 + i * .016, -.1 + j * .016, 0});
        if (leaf) assert(view.sees_leaf(leaf));
    }
    Reject([&] { visibility::Prepared bad(source, cuts, {0, 100, 1000}); });
    Reject([&] { visibility::Prepared bad(source, cuts, {4, 4, 1000}); });
    Reject([&] { visibility::Prepared bad(source, cuts, {4, 100, 10}); });
    Reject([&] { visibility::Prepared bad(nullptr, cuts); });
    auto invalid = Rooms(); invalid->nodes[0].children[0] = 0;
    Reject([&] { visibility::Prepared bad(invalid, cuts); });
    invalid = Rooms(); invalid->nodes[0].children[0] = std::numeric_limits<std::int32_t>::min();
    Reject([&] { visibility::Prepared bad(invalid, cuts); });
    invalid = Rooms(); invalid->pvs.pop_back();
    Reject([&] { visibility::Prepared bad(invalid, cuts); });
    invalid = Rooms(); invalid->planes[0].distance = std::numeric_limits<double>::infinity();
    Reject([&] { visibility::Prepared bad(invalid, cuts); });
    const std::vector flat{Hole(0, 0)};
    Reject([&] { visibility::Prepared bad(source, flat); });
    assert(prepared.view(1).sees_leaf(2)); // rejected candidates cannot change it
}
void SpatialLookup() {
    auto source = std::make_shared<visibility::Source>(); source->contents = {-2}; source->root = -1;
    std::vector<carving::Box> cuts;
    for (std::size_t i = 0; i < 4096; ++i) cuts.push_back(Hole(i * 4.0, i * 4.0 + 1));
    const visibility::Prepared prepared(source, cuts);
    assert(prepared.component_count() == cuts.size());
    const auto allocation_start = allocations.load();
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t sum = 0;
    constexpr std::size_t repeats = 262'144;
    for (std::size_t i = 0; i < repeats; ++i) {
        const auto index = (i * 293) % cuts.size();
        const auto key = prepared.key(0, {index * 4.0 + .5, 0, 0});
        assert(key == index + 1); sum += key;
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    assert(allocations.load() == allocation_start && sum);
    std::printf("{\"carvedVisibility\":true,\"spatialCuts\":%zu,\"queries\":%zu,\"queryAllocations\":0,\"queryCpuUs\":%.4f,\"passed\":true}\n",
        cuts.size(), repeats, elapsed / repeats);
}
} // namespace
int main() { Topology(); ObliqueAndFailures(); SpatialLookup(); }
