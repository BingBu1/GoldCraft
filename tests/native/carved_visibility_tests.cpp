#include "goldcraft/carved_visibility.hpp"
#include "goldcraft/entity_visibility.hpp"
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
void EntityInterest() {
    const auto source = Rooms();
    const std::vector cuts{Hole(0, 2), Hole(10, 12), Hole(30, 32), Hole(20.5, 21.5)};
    visibility::EntityCache cache(source, cuts);
    std::array<std::uint8_t, 4> mask{};
    assert(cache.merge(1, mask) && mask[0] == 7 && cache.matches(mask));
    assert(cache.sees_bounds(Hole(.5, 1.5)) && !cache.sees_bounds(Hole(30.5, 31.5)));
    cache.begin(); mask = {};
    assert(cache.merge(5, mask) && mask[0] == 24);
    assert(!cache.sees_bounds(Hole(.5, 1.5)) && cache.sees_bounds(Hole(30.5, 31.5)));
    assert(cache.merge(1, mask) && mask[0] == 31 && cache.matches(mask)); // fat boundary
    mask[0] = 0;
    assert(!cache.matches(mask)); // plugin narrowing must not gain cavity entities
    cache.begin(); mask = {};
    assert(cache.merge(cache.cavity_key({21, 0, 0}), mask) && mask[0] == 0);
    assert(cache.sees_bounds(Hole(20.6, 21.4)) && !cache.sees_bounds(cuts[0]));
    cache.begin(); mask = {};
    assert(!cache.merge(0, mask) && !cache.merge(99, mask));
    assert(!cache.sees_bounds(cuts[0]) && !cache.sees_bounds(cuts[3]));
    std::array<std::uint8_t, 9> oversized{};
    assert(!cache.merge(1, {}) && !cache.merge(1, oversized));
    auto directed = Rooms(); directed->pvs[0] = 3; directed->pvs[1] = 6;
    visibility::EntityCache unchanged(directed, {});
    assert(unchanged.merge(1, mask) && mask[0] == 3); // never transitive original PVS
    // Legacy FatPVS padding may extend past a 64-bit row. It must never read
    // beyond cached words or reject valid engine storage at that boundary.
    auto boundary = std::make_shared<visibility::Source>();
    boundary->contents.resize(65, -1); boundary->contents[0] = -2;
    boundary->root = -2; boundary->pvs.resize(64, 1);
    visibility::EntityCache padded(boundary, {});
    std::array<std::uint8_t, 11> legacy{};
    assert(padded.merge(1, legacy) && legacy[0] == 1 && padded.matches(legacy));
    legacy[10] = 1;
    assert(!padded.matches(legacy));
    Reject([&] { visibility::EntityCache too_large(source, cuts, {4, 15, 10000}); });
    Reject([&] { visibility::EntityCache too_slow(source, cuts, {4, 1000, 140}); });
    cache.begin(); mask = {};
    const auto before = allocations.load();
    const auto start = std::chrono::steady_clock::now();
    constexpr std::size_t repeats = 65'536;
    for (std::size_t i = 0; i < repeats; ++i) {
        cache.begin(); mask = {};
        assert(cache.merge(i % 2 ? 1 : 5, mask) && cache.matches(mask));
        assert(cache.sees_bounds(cuts[i % 2 ? 0 : 2]));
        assert(!cache.sees_bounds(cuts[3]));
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    assert(allocations.load() == before);
    std::printf("{\"entityVisibility\":true,\"queries\":%zu,\"queryAllocations\":0,\"queryCpuUs\":%.4f,\"fatBoundary\":true,\"sealedCavity\":true,\"originalNotTransitive\":true,\"customMaskPreserved\":true,\"passed\":true}\n", repeats, elapsed / repeats);
}
void DeliveryInterest() {
    auto directed = Rooms();
    directed->pvs = {3, 6, 12, 24, 16};
    visibility::EntityCache original(directed, {});
    assert(original.reaches(1, 2) && !original.reaches(1, 3));
    assert(original.reaches(1, 3, true) && !original.reaches(1, 4, true));
    assert(original.reaches(2, 4, true) && !original.reaches(2, 5, true));
    assert(!original.reaches(0, 1, true) && !original.reaches(1, 0, true));
    assert(!original.reaches(99, 1) && !original.reaches(1, 99, true));

    const std::vector cuts{Hole(0, 2), Hole(20.5, 21.5), Hole(30.5, 31.5)};
    visibility::EntityCache opened(directed, cuts);
    const auto connected = opened.key(0, {1, 0, 0});
    const auto sealed = opened.key(0, {21, 0, 0});
    const auto remote = opened.key(0, {31, 0, 0});
    assert(opened.reaches(1, 3) && opened.reaches(1, 4, true));
    assert(!opened.reaches(1, 4) && !opened.reaches(1, 5, true));
    assert(opened.reaches(connected, 3) && opened.reaches(connected, 4, true));
    assert(opened.reaches(1, connected) && !opened.reaches(1, sealed, true));
    assert(opened.reaches(sealed, sealed, true) && !opened.reaches(sealed, remote, true));
    assert(!opened.reaches(sealed, 1, true));
    std::array<std::uint8_t, 4> visible{}, audible{}, custom{};
    opened.begin(); opened.begin(true);
    assert(opened.merge(1, visible) && visible[0] == 7);
    assert(opened.merge(1, audible, true) && audible[0] == 15);
    assert(opened.matches(visible) && opened.matches(audible, true));
    assert(!opened.matches(custom, true));
    opened.begin(true); audible = {};
    assert(opened.merge(sealed, audible, true) && audible[0] == 0);
    assert(opened.sees_bounds(cuts[1], true) && !opened.sees_bounds(cuts[2], true));
    assert(opened.matches(visible)); // FatPAS must not overwrite FatPVS's cavity context.
    Reject([&] { visibility::EntityCache limit(directed, cuts, {4, opened.cached_words() - 1, 10000}); });
    // Budget the new PAS work as part of the same atomic preparation.
    Reject([&] { visibility::EntityCache limit(directed, cuts, {4, 1000, opened.operations() - 1}); });
    const auto before = allocations.load();
    const auto started = std::chrono::steady_clock::now();
    constexpr std::size_t repeats = 65'536;
    for (std::size_t i = 0; i < repeats; ++i) {
        assert(opened.reaches(i % 2 ? 1 : connected, 4, true));
        assert(!opened.reaches(i % 2 ? 1 : connected, sealed, true));
        assert(opened.matches(visible));
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    assert(allocations.load() == before);
    std::printf("{\"deliveryInterest\":true,\"queries\":%zu,\"allocations\":0,\"queryCpuUs\":%.4f,\"oneExpansion\":true,\"independentContexts\":true,\"sealedCavities\":true,\"passed\":true}\n", repeats, elapsed / repeats);
}
void ReusedEntityPreparation() {
    auto source = Rooms();
    source->pvs = {3, 6, 12, 24, 16};
    const std::vector cuts{Hole(0, 2), Hole(20.5, 21.5), Hole(30.5, 31.5)};
    auto previous = std::make_unique<visibility::EntityCache>(source, cuts);
    std::array<std::uint8_t, 4> visual{}, audible{}, zero{};
    assert(previous->merge(1, visual) && visual[0] == 7);
    assert(previous->merge(1, audible, true) && audible[0] == 15);
    visibility::EntityCache reused(source, cuts, {}, previous.get());
    assert(reused.shares_preparation(*previous));
    assert(reused.operations() == previous->operations());
    assert(reused.cached_words() == previous->cached_words());
    assert(reused.matches(zero) && reused.matches(zero, true));
    assert(!reused.sees_bounds(cuts[0]) && !reused.sees_bounds(cuts[0], true));
    assert(previous->matches(visual) && previous->matches(audible, true));
    const auto sealed = reused.cavity_key({21, 0, 0});
    std::array<std::uint8_t, 4> cavity{};
    assert(reused.merge(sealed, cavity) && cavity == zero);
    assert(reused.sees_bounds(cuts[1]) && !previous->sees_bounds(cuts[1]));
    assert(!reused.sees_bounds(cuts[1], true));
    assert(previous->matches(visual) && previous->matches(audible, true));
    previous->begin();
    assert(!previous->sees_bounds(cuts[0]) && reused.sees_bounds(cuts[1]));
    assert(previous->matches(audible, true));

    const visibility::EntityCache fresh(source, cuts);
    const auto keys = source->contents.size() + cuts.size();
    for (std::uint32_t from = 0; from < keys; ++from)
        for (std::uint32_t to = 0; to < keys; ++to)
            for (const bool sound : {false, true})
                assert(reused.reaches(from, to, sound) == fresh.reaches(from, to, sound));
    const visibility::EntityCache recaptured(std::make_shared<visibility::Source>(*source), cuts, {}, previous.get());
    assert(!recaptured.shares_preparation(*previous));
    auto changed = cuts;
    changed[0].min[0] = -0.0;
    const visibility::EntityCache signed_zero(source, changed, {}, previous.get());
    assert(!signed_zero.shares_preparation(*previous));
    changed = cuts;
    std::swap(changed[1], changed[2]);
    const visibility::EntityCache reordered(source, changed, {}, previous.get());
    assert(!reordered.shares_preparation(*previous));
    changed.pop_back();
    const visibility::EntityCache removed(source, changed, {}, previous.get());
    assert(!removed.shares_preparation(*previous));
    const visibility::EntityCache restored(source, {}, {}, previous.get());
    assert(!restored.shares_preparation(*previous) && restored.cavity_key({21, 0, 0}) == 0);
    auto limits = visibility::Limits{};
    --limits.boxes;
    const visibility::EntityCache changed_limits(source, cuts, limits, previous.get());
    assert(!changed_limits.shares_preparation(*previous));
    limits = {};
    limits.words = previous->cached_words() - 1;
    Reject([&] { visibility::EntityCache invalid(source, cuts, limits, previous.get()); });
    limits = {};
    limits.operations = previous->operations() - 1;
    Reject([&] { visibility::EntityCache invalid(source, cuts, limits, previous.get()); });
    changed = cuts;
    changed[0].min[0] = std::numeric_limits<double>::quiet_NaN();
    Reject([&] { visibility::EntityCache invalid(source, changed, {}, previous.get()); });
    assert(previous->matches(audible, true) && reused.sees_bounds(cuts[1]));
    previous.reset();
    assert(reused.reaches(1, 3) && reused.reaches(1, 4, true));
    assert(reused.sees_bounds(cuts[1]));
}
void SameTopology(const visibility::Prepared& a, const visibility::Prepared& b,
                  const std::vector<carving::Box>& cuts) {
    assert(a.leaf_count() == b.leaf_count() && a.component_count() == b.component_count());
    assert(a.operations() == b.operations());
    for (std::uint32_t key = 0; key < a.leaf_count() + a.component_count(); ++key) {
        const auto av = a.view(key), bv = b.view(key);
        assert(av.leaves == bv.leaves && av.components == bv.components && av.operations == bv.operations);
        for (const auto& cut : cuts) assert(a.sees_bounds(av, cut) == b.sees_bounds(bv, cut));
    }
    for (const auto& cut : cuts) {
        carving::Point p{};
        for (int axis = 0; axis < 3; ++axis) p[axis] = (cut.min[axis] + cut.max[axis]) / 2;
        assert(a.key(0, p) == b.key(0, p));
    }
}
void ReusedSourceValidation() {
    const auto source = Rooms();
    const std::vector cuts{Hole(0, 2), Hole(10, 12)};
    auto previous = std::make_shared<visibility::Prepared>(source, cuts);
    assert(!previous->reused_validation());
    const std::vector changed{Hole(30, 32), cuts[0], cuts[1]};
    const visibility::Prepared cold(source, changed);
    const visibility::Prepared warm(source, changed, {}, previous.get());
    assert(warm.reused_validation());
    SameTopology(warm, cold, changed);
    const visibility::Prepared recaptured(std::make_shared<visibility::Source>(*source), changed, {}, previous.get());
    assert(!recaptured.reused_validation());
    SameTopology(recaptured, cold, changed);

    auto limits = visibility::Limits{};
    limits.operations = cold.operations();
    SameTopology(visibility::Prepared(source, changed, limits, previous.get()), cold, changed);
    --limits.operations;
    Reject([&] { visibility::Prepared invalid(source, changed, limits, previous.get()); });
    Reject([&] { visibility::Prepared invalid(source, changed, limits); });
    // A new capture must still check contents, all negative child references,
    // planes and reachable topology before using any prior validation result.
    for (int fault = 0; fault < 6; ++fault) {
        auto malformed = std::make_shared<visibility::Source>(*source);
        switch (fault) {
        case 0: malformed->contents.back() = 0; break;
        case 1: malformed->nodes.push_back({0, {-100, -1}}); break;
        case 2: malformed->planes[0].normal[0] = 2; break;
        case 3: malformed->nodes[0].children[0] = 0; break;
        case 4: malformed->root = -100; break;
        case 5: malformed->nodes[0].plane = 100; break;
        }
        Reject([&] { visibility::Prepared invalid(malformed, changed, {}, previous.get()); });
    }
    SameTopology(*previous, visibility::Prepared(source, cuts), cuts);
    const std::weak_ptr<const visibility::Prepared> owner = previous;
    previous.reset();
    assert(owner.expired());
    SameTopology(warm, cold, changed);
    const visibility::Prepared cleared(source, {}, {}, &warm);
    assert(cleared.reused_validation() && cleared.component_count() == 0);
    SameTopology(cleared, visibility::Prepared(source, {}), {});

    auto leaf = std::make_shared<visibility::Source>();
    leaf->root = -2; leaf->contents = {-2, -1}; leaf->pvs = {1};
    const visibility::Prepared leaf_previous(leaf, {});
    limits = {};
    limits.operations = leaf_previous.operations();
    SameTopology(visibility::Prepared(leaf, {}, limits, &leaf_previous), leaf_previous, {});
    --limits.operations; // Exactly zero work remains for validate_hull.
    for (const auto* prior : {static_cast<const visibility::Prepared*>(nullptr), &leaf_previous}) {
        bool rejected = false;
        try { visibility::Prepared invalid(leaf, {}, limits, prior); }
        catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }

    // The validator's full-array limit is independent of traversal work:
    // these nodes are unreachable, but their size still bounds admission.
    auto padded = std::make_shared<visibility::Source>(*leaf);
    padded->nodes.resize(128, {0, {-1, -1}});
    const visibility::Prepared padded_previous(padded, {});
    limits = {};
    limits.operations = padded_previous.operations();
    Reject([&] { visibility::Prepared invalid(padded, {}, limits); });
    Reject([&] { visibility::Prepared invalid(padded, {}, limits, &padded_previous); });
    limits.operations = padded->pvs.size() + padded->contents.size() + 2 * padded->nodes.size();
    SameTopology(visibility::Prepared(padded, {}, limits, &padded_previous),
                 visibility::Prepared(padded, {}, limits), {});
}
void ReusedCutContacts() {
    constexpr std::size_t cache_bytes = 4096;
    auto source = Rooms();
    const std::vector cuts{Hole(0, 2), Hole(10, 12), Hole(20.5, 21.5)};
    auto previous = std::make_unique<visibility::Prepared>(source, cuts, visibility::Limits{}, nullptr, cache_bytes);
    assert(previous->contact_cache_bytes() && previous->contact_cache_bytes() <= cache_bytes);
    const std::vector changed{cuts[2], cuts[0], Hole(2, 10), cuts[1], cuts[0]};
    const visibility::Prepared cold(source, changed);
    const visibility::Prepared reused(source, changed, {}, previous.get(), cache_bytes);
    assert(reused.reused_contacts() == 4 && reused.contact_cache_bytes() <= cache_bytes);
    SameTopology(reused, cold, changed);
    const visibility::Prepared removed(source, cuts, {}, &reused, cache_bytes);
    SameTopology(removed, *previous, cuts);
    assert(removed.reused_contacts() == cuts.size());
    const visibility::Prepared no_cache(source, changed, {}, previous.get());
    assert(no_cache.reused_contacts() == 4 && no_cache.contact_cache_bytes() == 0);
    SameTopology(no_cache, cold, changed);
    const visibility::Prepared too_small(source, changed, {}, previous.get(), 1);
    assert(too_small.contact_cache_bytes() == 0);
    SameTopology(too_small, cold, changed);
    const visibility::Prepared partial(source, cuts, {}, nullptr, 256);
    const visibility::Prepared partial_next(source, cuts, {}, &partial, 256);
    assert(partial_next.contact_cache_bytes() <= 256);
    assert(partial_next.reused_contacts() > 0 && partial_next.reused_contacts() < cuts.size());
    SameTopology(partial_next, *previous, cuts);
    const visibility::Prepared recaptured(std::make_shared<visibility::Source>(*source), changed, {}, previous.get(), cache_bytes);
    assert(recaptured.reused_contacts() == 0);
    SameTopology(recaptured, cold, changed);
    auto signed_zero = cuts;
    signed_zero[0].min[0] = -0.0;
    const visibility::Prepared different(source, signed_zero, {}, previous.get(), cache_bytes);
    assert(different.reused_contacts() == 2);
    SameTopology(different, visibility::Prepared(source, signed_zero), signed_zero);
    auto limits = visibility::Limits{};
    limits.operations = cold.operations();
    SameTopology(visibility::Prepared(source, changed, limits, previous.get(), cache_bytes), cold, changed);
    --limits.operations;
    Reject([&] { visibility::Prepared invalid(source, changed, limits, previous.get(), cache_bytes); });
    const visibility::Prepared cleared(source, {}, {}, previous.get(), cache_bytes);
    assert(cleared.component_count() == 0 && cleared.contact_cache_bytes() == 0);
    previous.reset();
    SameTopology(reused, cold, changed);

    // Shared BSP children can visit the same leaf repeatedly. Cache the visit
    // sequence, not a set, so contact storage and operation limits stay exact.
    auto dag = std::make_shared<visibility::Source>();
    dag->contents = {-2, -1, -1}; dag->pvs = {1, 2};
    dag->planes = {{{1,0,0},0}, {{0,1,0},0}};
    dag->nodes = {{0,{1,1}}, {1,{-2,-3}}};
    const std::vector dag_cuts{Hole(-1, 1)};
    const visibility::Prepared dag_previous(dag, dag_cuts, {}, nullptr, cache_bytes);
    const visibility::Prepared dag_reused(dag, dag_cuts, {}, &dag_previous, cache_bytes);
    assert(dag_reused.reused_contacts() == 1);
    SameTopology(dag_reused, dag_previous, dag_cuts);
    Reject([&] { visibility::Prepared invalid(dag, dag_cuts, {8,3,1000}, &dag_previous, cache_bytes); });
    Reject([&] { visibility::Prepared invalid(dag, dag_cuts, {8,3,1000}); });

    visibility::EntityCache old_rows(source, cuts);
    visibility::EntityCache new_rows(source, changed, {}, &old_rows);
    visibility::EntityCache fresh_rows(source, changed);
    assert(!new_rows.shares_preparation(old_rows) && new_rows.reused_contacts() == 4);
    assert(new_rows.operations() == fresh_rows.operations() && new_rows.cached_words() == fresh_rows.cached_words());
    assert(new_rows.contact_cache_bytes() <= 16 * 1024 * 1024);
    const auto keys = cold.leaf_count() + cold.component_count();
    for (std::uint32_t from = 0; from < keys; ++from)
        for (std::uint32_t to = 0; to < keys; ++to)
            for (const bool sound : {false, true})
                assert(new_rows.reaches(from, to, sound) == fresh_rows.reaches(from, to, sound));
}
} // namespace
int main() { Topology(); ObliqueAndFailures(); SpatialLookup(); EntityInterest(); DeliveryInterest(); ReusedEntityPreparation(); ReusedSourceValidation(); ReusedCutContacts(); }
