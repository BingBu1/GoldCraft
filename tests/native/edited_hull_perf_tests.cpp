// Real BSP query corpus and allocation/lifetime checks. An independently built
// pre-change executable writes the optional comparison file; no old algorithm
// is duplicated here. Timings describe CPU queries, never game frame rate.
#include "goldcraft/edited_hull.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef GOLDCRAFT_COLLISION_REUSE
#define GOLDCRAFT_COLLISION_REUSE 1
#endif
namespace allocation {
struct alignas(std::max_align_t) Header {
    std::size_t bytes;
};
std::atomic<std::size_t> count{}, live{};
thread_local void (*callback)() = nullptr;
thread_local int fail_after = -1;
} // namespace allocation
void *operator new(std::size_t bytes) {
    if (allocation::callback)
        allocation::callback();
    if (allocation::fail_after == 0)
        throw std::bad_alloc();
    if (allocation::fail_after > 0)
        --allocation::fail_after;
    if (bytes > std::numeric_limits<std::size_t>::max() - sizeof(allocation::Header))
        throw std::bad_alloc();
    auto *p = static_cast<allocation::Header *>(std::malloc(sizeof(allocation::Header) + bytes));
    if (!p)
        throw std::bad_alloc();
    p->bytes = bytes;
    ++allocation::count;
    allocation::live += bytes;
    return p + 1;
}
void operator delete(void *p) noexcept {
    if (!p)
        return;
    const auto header = static_cast<allocation::Header *>(p) - 1;
    allocation::live -= header->bytes;
    std::free(header);
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }

using namespace goldcraft::carving;
namespace {
struct Bsp {
    std::vector<unsigned char> bytes;
    std::array<std::size_t, 15> offset{}, length{};
    std::vector<Plane> planes;
    std::array<std::vector<HullNode>, 2> nodes;
    std::array<int, 4> roots;
    unsigned u16(std::size_t p) const {
        assert(p + 2 <= bytes.size());
        return bytes[p] | (unsigned(bytes[p + 1]) << 8);
    }
    unsigned u32(std::size_t p) const { return u16(p) | (u16(p + 2) << 16); }
    double f32(std::size_t p) const { return std::bit_cast<float>(u32(p)); }
    explicit Bsp(const char *path) {
        std::ifstream file(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(file), {});
        assert(bytes.size() >= 124 && u32(0) == 30);
        std::uint32_t crc = ~0u;
        for (auto byte : bytes) {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
        assert(~crc == 0xf6725c06u);
        for (int i = 0; i < 15; ++i) {
            offset[i] = u32(4 + i * 8);
            length[i] = u32(8 + i * 8);
            assert(offset[i] + length[i] <= bytes.size());
        }
        for (std::size_t p = offset[1]; p < offset[1] + length[1]; p += 20)
            planes.push_back({{f32(p), f32(p + 4), f32(p + 8)}, f32(p + 12)});
        for (std::size_t p = offset[5]; p < offset[5] + length[5]; p += 24) {
            HullNode n{u32(p), {}};
            for (int side = 0; side < 2; ++side) {
                const auto child =
                    std::bit_cast<std::int16_t>(std::uint16_t(u16(p + 4 + side * 2)));
                n.children[side] =
                    child >= 0 ? child : int(u32(offset[10] + std::size_t(-1 - child) * 28));
            }
            nodes[0].push_back(n);
        }
        for (std::size_t p = offset[9]; p < offset[9] + length[9]; p += 8)
            nodes[1].push_back({u32(p),
                                {std::bit_cast<std::int16_t>(std::uint16_t(u16(p + 4))),
                                 std::bit_cast<std::int16_t>(std::uint16_t(u16(p + 6)))}});
        for (int h = 0; h < 4; ++h)
            roots[h] = int(u32(offset[14] + 36 + h * 4));
    }
    HullView hull(int h) const { return {planes, nodes[h ? 1 : 0], roots[h]}; }
};
struct Query {
    Point a, b;
    double margin = .03125;
};
struct QueryResult {
    std::array<std::uint64_t, 9> words;
    bool operator==(const QueryResult &) const = default;
};
QueryResult result(const EditedHull &edit, const Query &q) {
    const auto tr = edit.trace(q.a, q.b, q.margin);
    QueryResult r{};
    r.words[0] = std::bit_cast<std::uint64_t>(tr.fraction);
    for (int axis = 0; axis < 3; ++axis)
        r.words[axis + 1] = std::bit_cast<std::uint64_t>(tr.plane.normal[axis]);
    r.words[4] = std::bit_cast<std::uint64_t>(tr.plane.distance);
    r.words[5] = tr.start_solid | (tr.all_solid << 1) | (tr.in_open << 2) | (tr.in_water << 3);
    r.words[6] = std::uint64_t(edit.contents(q.a));
    r.words[7] = std::uint64_t(edit.contents(q.b));
    r.words[8] = edit.affects(q.a, q.b);
    return r;
}
std::uint64_t digest(std::uint64_t hash, const QueryResult &r) {
    // affects is deliberately narrower; all observable trace/contents fields
    // must remain bit-exact against the independently compiled old version.
    for (std::size_t i = 0; i < 8; ++i)
        hash = (hash ^ r.words[i]) * 1099511628211ull;
    return hash;
}
struct Random {
    std::uint32_t state = 0x739125ab;
    double get() {
        state = state * 1664525u + 1013904223u;
        return double(state) / 4294967296.0;
    }
};
std::vector<Box> holes(int count) {
    std::vector<Box> cuts;
    for (int i = 0; i < count; ++i) {
        const double x = 64 + (i / 2) * 128 + (i % 2) * 32;
        cuts.push_back({{x, 2864, 358.4}, {x + 32, 2960, 454.4}});
    }
    return cuts;
}
std::vector<Query> queries(const std::vector<Box> &cuts, Box body) {
    std::vector<Query> out;
    for (const auto &cut : cuts) {
        Point middle{};
        for (int axis = 0; axis < 3; ++axis)
            middle[axis] = (cut.min[axis] + cut.max[axis]) * .5;
        for (int axis = 0; axis < 3; ++axis)
            for (double boundary : {cut.min[axis] - body.max[axis], cut.min[axis] - body.min[axis],
                                    cut.max[axis] - body.max[axis], cut.max[axis] - body.min[axis]})
                for (double value : {std::nextafter(boundary, -INFINITY), boundary,
                                     std::nextafter(boundary, INFINITY)}) {
                    auto a = middle, b = middle;
                    a[axis] = b[axis] = value;
                    a[(axis + 1) % 3] -= 192;
                    b[(axis + 1) % 3] += 192;
                    out.push_back({a, b});
                    out.push_back({b, a, 0});
                    out.push_back({a, a});
                }
        out.push_back({{middle[0], 2784, middle[2]}, {middle[0], 2976, middle[2]}});
    }
    Random random;
    for (int i = 0; i < 1536; ++i) {
        Point a{}, b{};
        for (int axis = 0; axis < 3; ++axis) {
            const auto low = cuts.front().min[axis] - 192, high = cuts.back().max[axis] + 192;
            a[axis] = low + (high - low) * random.get();
            b[axis] = i % 3 ? a[axis] + random.get() * 96 - 48 : low + (high - low) * random.get();
        }
        out.push_back({a, b, i % 2 ? .03125 : 0});
    }
    return out;
}
thread_local const EditedHull *nested_edit;
thread_local Query nested_query;
thread_local QueryResult nested_expected;
thread_local std::array<bool, 7> nested_seen{};
thread_local int nested_depth;
void nested_allocation() {
    if (nested_depth == 6 || nested_seen[nested_depth])
        return;
    nested_seen[nested_depth] = true;
    ++nested_depth;
    assert(result(*nested_edit, nested_query) == nested_expected);
    --nested_depth;
}
void lifetimes(const EditedHull &edit, Query q) {
    const auto expected = result(edit, q);
    std::size_t cold = 0, warm = 0;
    std::thread thread([&] {
        nested_edit = &edit;
        nested_query = q;
        nested_expected = expected;
        allocation::callback = nested_allocation;
        const auto before = allocation::count.load();
        assert(result(edit, q) == expected);
        allocation::callback = nullptr;
        cold = allocation::count.load() - before;
        assert(std::all_of(nested_seen.begin(), nested_seen.begin() + 6, [](bool b) { return b; }));
        const auto warm_before = allocation::count.load();
        for (int i = 0; i < 256; ++i)
            assert(result(edit, q) == expected);
        warm = allocation::count.load() - warm_before;
    });
    thread.join();
    std::thread failure([&] {
        allocation::fail_after = 2;
        bool failed = false;
        try {
            (void)result(edit, q);
        } catch (const std::bad_alloc &) {
            failed = true;
        }
        allocation::fail_after = -1;
        assert(failed && result(edit, q) == expected);
        for (double margin : {-1.0, 1.1, std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected = false;
            try {
                (void)edit.trace(q.a, q.b, margin);
            } catch (const std::invalid_argument &) {
                rejected = true;
            }
            assert(rejected && result(edit, q) == expected);
        }
        const auto before = allocation::count.load();
        for (int i = 0; i < 256; ++i)
            assert(result(edit, q) == expected);
#if GOLDCRAFT_COLLISION_REUSE
        assert(allocation::count.load() == before);
#else
        (void)before;
#endif
    });
    failure.join();
#if GOLDCRAFT_COLLISION_REUSE
    assert(warm == 0);
#endif
    std::atomic<int> passed{};
    std::array<std::thread, 4> workers;
    for (auto &worker : workers)
        worker = std::thread([&] {
            for (int i = 0; i < 512; ++i)
                assert(result(edit, q) == expected);
            ++passed;
        });
    for (auto &worker : workers)
        worker.join();
    assert(passed == 4);
    std::cout << "{\"queryLifetime\":true,\"nestedDepth\":6,\"nestedColdAllocations\":" << cold
              << ",\"warmAllocations\":" << warm << ",\"badAllocRecovery\":true,\"threads\":4}\n";
}
void retention() {
    std::vector<Plane> planes;
    std::vector<HullNode> nodes;
    const auto build = [&](auto &&self, int begin, int end) -> int {
        if (end - begin == 1)
            return begin % 2 ? -2 : -1;
        const int middle = (begin + end) / 2, slot = int(nodes.size());
        planes.push_back({{1, 0, 0}, double(middle * 2)});
        nodes.push_back({unsigned(slot), {}});
        const int front = self(self, middle, end), back = self(self, begin, middle);
        nodes[slot].children = {front, back};
        return slot;
    };
    const int root = build(build, 0, 8192);
    const Box cut{{32, -1, -1}, {33, 1, 1}}, point{{}, {}};
    const EditedHull edit({{}, {}, -2}, {planes, nodes, root}, point, {&cut, 1});
    std::size_t retained = 0;
    std::thread thread([&] {
        const auto before = allocation::live.load();
        (void)edit.trace({0, 0, 0}, {16384, 0, 0});
        retained = allocation::live.load() - before;
    });
    thread.join();
#if GOLDCRAFT_COLLISION_REUSE
    assert(retained <= 256 * 1024);
#endif
    std::cout << "{\"oversizedQuery\":true,\"nativeLeaves\":8192,\"retainedBytes\":" << retained
              << "}\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 4)
            throw std::invalid_argument(
                "Usage: edited_hull_perf_tests cs_assault.bsp [--write|--compare corpus.bin]");
        std::ofstream saved;
        std::ifstream previous;
        if (argc == 4) {
            if (std::string(argv[2]) == "--write")
                saved.open(argv[3], std::ios::binary);
            else if (std::string(argv[2]) == "--compare")
                previous.open(argv[3], std::ios::binary);
            else
                throw std::invalid_argument("Unknown comparison mode");
            if (!saved.is_open() && !previous.is_open())
                throw std::runtime_error("Comparison file unavailable");
        }
        const Bsp bsp(argv[1]);
        const std::array<Box, 4> bodies{Box{{}, {}}, Box{{-16, -16, -36}, {16, 16, 36}},
                                        Box{{-32, -32, -32}, {32, 32, 32}},
                                        Box{{-16, -16, -18}, {16, 16, 18}}};
        std::size_t compared = 0, narrowed = 0;
        for (int count : {2, 8, 24}) {
            const auto cuts = holes(count);
            for (int h = 0; h < 4; ++h) {
                const EditedHull edit(bsp.hull(0), bsp.hull(h), bodies[h], cuts);
                const auto corpus = queries(cuts, bodies[h]);
                std::vector<QueryResult> expected;
                expected.reserve(corpus.size());
                std::size_t cold = 0, first_allocations = 0;
                double first_us = 0;
                std::thread prepare([&] {
                    const auto before = allocation::count.load();
                    const auto began = std::chrono::steady_clock::now();
                    expected.push_back(result(edit, corpus.front()));
                    first_us = std::chrono::duration<double, std::micro>(
                                   std::chrono::steady_clock::now() - began)
                                   .count();
                    first_allocations = allocation::count.load() - before;
                    for (std::size_t i = 1; i < corpus.size(); ++i)
                        expected.push_back(result(edit, corpus[i]));
                    cold = allocation::count.load() - before;
                });
                prepare.join();
                for (const auto &r : expected) {
                    if (saved.is_open())
                        saved.write(reinterpret_cast<const char *>(&r), sizeof(r));
                    if (previous.is_open()) {
                        QueryResult old{};
                        previous.read(reinterpret_cast<char *>(&old), sizeof(old));
                        if (!previous ||
                            !std::equal(r.words.begin(), r.words.begin() + 8, old.words.begin()) ||
                            r.words[8] > old.words[8])
                            throw std::runtime_error(
                                "Differential mismatch at query " + std::to_string(compared) +
                                " hull=" + std::to_string(h) + " cuts=" + std::to_string(count));
                        narrowed += old.words[8] != r.words[8];
                        ++compared;
                    }
                }
                for (const auto &q : corpus)
                    (void)result(edit, q); // Warm all sizes, no huge reserve.
                const auto before = allocation::count.load();
                const auto began = std::chrono::steady_clock::now();
                std::uint64_t hash = 1469598103934665603ull;
                constexpr int repeats = 4;
                for (int repeat = 0; repeat < repeats; ++repeat)
                    for (std::size_t i = 0; i < corpus.size(); ++i) {
                        const auto r = result(edit, corpus[i]);
                        assert(r == expected[i]);
                        hash = digest(hash, r);
                    }
                const auto elapsed = std::chrono::duration<double, std::micro>(
                                         std::chrono::steady_clock::now() - began)
                                         .count();
                const auto allocated = allocation::count.load() - before;
#if GOLDCRAFT_COLLISION_REUSE
                assert(allocated == 0);
#endif
                std::cout << "{\"csAssault\":true,\"cuts\":" << count << ",\"hull\":" << h
                          << ",\"queries\":" << corpus.size() * repeats
                          << ",\"newThreadCorpusAllocations\":" << cold
                          << ",\"firstQueryAllocations\":" << first_allocations
                          << ",\"firstQueryCpuUs\":" << first_us
                          << ",\"warmAllocations\":" << allocated
                          << ",\"queryCpuUs\":" << elapsed / (corpus.size() * repeats)
                          << ",\"digest\":" << hash << "}\n";
                if (count == 2 && h == 1) {
                    const Query opening{{96, 2784, 406.4}, {96, 2976, 406.4}};
                    const auto tr = edit.trace(opening.a, opening.b);
                    assert(tr.fraction == 1 && !tr.start_solid && !tr.all_solid);
                    lifetimes(edit, opening);
                }
            }
        }
        retention();
        if (previous.is_open()) {
            assert(previous.peek() == std::char_traits<char>::eof());
            std::cout << "{\"differentialQueries\":" << compared
                      << ",\"narrowedBroadPhase\":" << narrowed
                      << ",\"traceContentsBitExact\":true}\n";
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
