#include "goldcraft/world_volume.hpp"
#include "goldcraft/edited_hull.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace goldcraft::carving;

namespace {
Point sub(Point a, const Point &b) {
    for (int i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
Point cross(const Point &a, const Point &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Point &a, const Point &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void close(double a, double b, double tolerance = 1e-8) {
    if (std::abs(a - b) > tolerance)
        throw std::runtime_error("Volume/trace mismatch: " + std::to_string(a) + " vs " + std::to_string(b));
}
template <class Exception, class Function> void rejects(Function operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const Exception &) {
        rejected = true;
    }
    assert(rejected);
}
double measure(const Volume &volume) {
    double total = 0;
    for (const auto &cell : volume.cells) {
        // Divergence theorem, based on actual vertices and winding, independent
        // of the halfspace tests used by the collision implementation.
        const auto origin = cell.faces.front().vertices.front();
        double value = 0;
        for (const auto &face : cell.faces) {
            assert(face.vertices.size() >= 3);
            for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
                const auto normal = cross(sub(face.vertices[i], face.vertices[0]),
                                          sub(face.vertices[i + 1], face.vertices[0]));
                assert(dot(normal, face.plane.normal) >= -1e-12);
                value += dot(sub(face.vertices[0], origin),
                             cross(sub(face.vertices[i], origin), sub(face.vertices[i + 1], origin))) /
                         6;
            }
            for (const auto &vertex : face.vertices)
                close(dot(vertex, face.plane.normal), face.plane.distance);
            for (const auto &other : cell.faces)
                for (const auto &vertex : other.vertices)
                    assert(dot(vertex, face.plane.normal) <= face.plane.distance + 1e-8);
        }
        assert(value > 0);
        total += value;
    }
    return total;
}
struct Random {
    std::uint32_t state = 0x739125ab;
    double operator()() {
        state = state * 1664525u + 1013904223u;
        return double(state) / 4294967296.0;
    }
};
constexpr HullView solid{{}, {}, -2}, empty{{}, {}, -1}, water{{}, {}, -3};
constexpr Box point{{0, 0, 0}, {0, 0, 0}};

void conservation() {
    const Box region{{-4, -4, -4}, {4, 4, 4}};
    const Box a{{-2, -2, -2}, {2, 2, 2}}, b{{0, -2, -2}, {4, 2, 2}};
    const std::array<Box, 4> cuts{a, b, a, Box{{-1, -1, -1}, {1, 1, 1}}};
    close(measure(carve_volume(solid, region, {})), 512);
    close(measure(carve_volume(solid, region, cuts)), 512 - 96);
    assert(carve_volume(empty, region, cuts).cells.empty());
    assert(carve_volume(water, region, cuts).cells.empty());
    assert(carve_volume(solid, region, {&region, 1}).cells.empty());
    auto reversed = cuts;
    std::ranges::reverse(reversed);
    auto whole = carve_volume(solid, region, cuts);
    const auto other = expand_volume(carve_volume(solid, region, reversed), point);
    const auto volume = expand_volume(whole, point);
    Random random;
    for (int i = 0; i < 20000; ++i) {
        Point p{};
        for (auto &v : p)
            v = random() * 8 - 4;
        const bool removed = p[0] > -2 && p[0] < 4 && p[1] > -2 && p[1] < 2 && p[2] > -2 && p[2] < 2;
        assert(contains(volume, p) == !removed);
        assert(contains(other, p) == !removed);
    }
    // Permutation, duplicate and subset cuts have the same geometric union.
    close(measure(carve_volume(solid, region, reversed)), measure(whole));
    const Box outside{{8, 8, 8}, {12, 12, 12}};
    close(measure(carve_volume(solid, region, {&outside, 1})), 512);
}

void openings() {
    // A thin host wall, with air in front and behind. A body's center must enter
    // through outside air even while only its leading part lies in the cut.
    const Box wall{{-128, 0, -128}, {128, 16, 128}};
    const Box standing{{-16, -16, -36}, {16, 16, 36}}, crouching{{-16, -16, -18}, {16, 16, 18}};
    const std::array<Box, 2> cuts{Box{{-32, 0, -48}, {0, 16, 48}}, Box{{0, 0, -48}, {32, 16, 48}}};
    auto carved = carve_volume(solid, wall, cuts);
    close(measure(carved), 256 * 16 * 256 - 64 * 16 * 96);
    const auto full = expand_volume(carved, standing);
    auto trace = trace_volume(full, {0, -64, 0}, {0, 64, 0});
    assert(trace.fraction == 1 && !trace.start_solid && !trace.all_solid);
    assert(!contains(full, {0, -8, 0}));
    // The left box alone cannot fit a centered body across the shared edge.
    const auto single = expand_volume(carve_volume(solid, wall, {cuts.data(), 1}), standing);
    assert(trace_volume(single, {0, -64, 0}, {0, 64, 0}).fraction < 1);
    assert(trace_volume(full, {20, -64, 0}, {20, 64, 0}).fraction < 1);
    const Box low{{-32, 0, -20}, {32, 16, 20}};
    auto low_wall = carve_volume(solid, wall, {&low, 1});
    assert(trace_volume(expand_volume(low_wall, standing), {0, -64, 0}, {0, 64, 0}).fraction < 1);
    assert(trace_volume(expand_volume(low_wall, crouching), {0, -64, 0}, {0, 64, 0}).fraction == 1);
    const Box feet{{-16, -16, 0}, {16, 16, 72}};
    assert(trace_volume(expand_volume(carved, feet), {0, -64, -36}, {0, 64, -36}).fraction == 1);
    assert(trace_volume(expand_volume(carved, feet), {0, -64, 0}, {0, 64, 0}).fraction < 1);
    // With no hole, margin backs off from the original surface by 1/32 units.
    const auto uncut = expand_volume(carve_volume(solid, wall, {}), standing);
    trace = trace_volume(uncut, {0, -64, 0}, {0, 64, 0});
    close(trace.fraction, (48.0 - 0.03125) / 128);
    close(trace.plane.normal[1], -1);
    close(trace.plane.distance, 16);
}

void bevels() {
    // Positive unit tetrahedron. Its intersection with a translated AABB has
    // a closed-form oracle: the sum of the positive lower corner coordinates
    // must be < 1, and every upper coordinate must be > 0. This detects the
    // edge/axis bevels absent from a simple offset of original face planes.
    const double n = 1 / std::sqrt(3.0);
    const std::array<Plane, 4> planes{Plane{{-1, 0, 0}, 0}, Plane{{0, -1, 0}, 0}, Plane{{0, 0, -1}, 0},
                                      Plane{{n, n, n}, n}};
    const std::array<HullNode, 4> nodes{HullNode{0, {-1, 1}}, HullNode{1, {-1, 2}}, HullNode{2, {-1, 3}},
                                        HullNode{3, {-1, -2}}};
    auto tetra = carve_volume({planes, nodes, 0}, {{-2, -2, -2}, {2, 2, 2}}, {});
    close(measure(tetra), 1.0 / 6);
    const std::array<Box, 4> bodies{point, Box{{-.2, -.2, -.2}, {.2, .2, .2}},
                                    Box{{-.4, 0, -.1}, {.3, .1, .7}}, Box{{0, 0, 0}, {.5, .3, .2}}};
    Random random;
    for (const auto &body : bodies) {
        const auto collision = expand_volume(tetra, body);
        for (int sample = 0; sample < 20000; ++sample) {
            Point p{};
            double sum = 0;
            bool positive = true;
            for (int i = 0; i < 3; ++i) {
                p[i] = random() * 3 - 1;
                positive &= p[i] + body.max[i] > 0;
                sum += std::max(0.0, p[i] + body.min[i]);
            }
            assert(contains(collision, p) == (positive && sum < 1));
        }
    }
    const auto collision = expand_volume(tetra, bodies[1]);
    assert(!contains(collision, {.9, .6, -.15}));
    const auto trace = trace_volume(collision, {1.5, .6, -.15}, {0, .6, -.15}, 0);
    close(trace.fraction, .7 / 1.5);
    close(trace.plane.normal[0], 1 / std::sqrt(2.0));
    close(trace.plane.normal[1], 1 / std::sqrt(2.0));
    close(trace.plane.normal[2], 0);

    const std::array<Plane, 1> slope{Plane{{n, n, n}, 0}};
    const std::array<HullNode, 1> slope_node{HullNode{0, {-1, -2}}};
    auto diagonal = carve_volume({slope, slope_node, 0}, {{-10, -10, -10}, {10, 10, 10}}, {});
    close(measure(diagonal), 4000);
    const auto hit = trace_volume(expand_volume(diagonal, bodies[1]), {1, 1, 1}, {-1, -1, -1}, 0);
    close(hit.fraction, .4);
    for (auto value : hit.plane.normal)
        close(value, n);
}

void traces() {
    const std::array<Plane, 1> planes{Plane{{1, 0, 0}, 0}};
    const std::array<HullNode, 1> nodes{HullNode{0, {-2, -2}}};
    const Box region{{-2, -2, -2}, {2, 2, 2}};
    auto split = carve_volume({planes, nodes, 0}, region, {});
    assert(split.cells.size() == 2);
    close(measure(split), 64);
    const auto volume = expand_volume(split, point);
    // Trace on the partition itself; the union must not have a zero-width gap.
    auto hit = trace_volume(volume, {0, -4, 0}, {0, 4, 0}, 0);
    close(hit.fraction, .25);
    hit = trace_volume(volume, {0, -1, 0}, {0, 1, 0});
    assert(hit.start_solid && hit.all_solid && hit.fraction == 1);
    hit = trace_volume(volume, {-1, 0, 0}, {1, 0, 0});
    assert(hit.start_solid && hit.all_solid);
    hit = trace_volume(volume, {0, 0, 0}, {0, 4, 0});
    assert(hit.start_solid && !hit.all_solid && hit.fraction == 1);
    hit = trace_volume(volume, {0, 0, 0}, {0, 0, 0});
    assert(hit.start_solid && hit.all_solid && hit.fraction == 1);
    hit = trace_volume(volume, {0, 4, 0}, {0, 4, 0});
    assert(!hit.start_solid && !hit.all_solid && hit.fraction == 1);
    const Box gap{{-1, -4, -4}, {1, 4, 4}};
    const auto disjoint = expand_volume(carve_volume(solid, region, {&gap, 1}), point);
    hit = trace_volume(disjoint, {-1.5, 0, 0}, {1.5, 0, 0}, 0);
    assert(hit.start_solid && !hit.all_solid);
    close(hit.fraction, 2.5 / 3);
    hit = trace_volume(volume, {-2, 0, 0}, {-4, 0, 0}, 0);
    assert(hit.start_solid && !hit.all_solid && hit.fraction == 1);
    hit = trace_volume(volume, {2, 0, 0}, {0, 0, 0}, 0);
    assert(!hit.start_solid && !hit.all_solid && hit.fraction == 0);
    hit = trace_volume(volume, {0, 0, 0}, {2, 0, 0}, 0);
    assert(hit.start_solid && !hit.all_solid && hit.fraction == 1);
    hit = trace_volume(volume, {0, 0, 0}, {-2, 0, 0}, 0);
    assert(hit.start_solid && hit.all_solid && hit.fraction == 1);
    // The cut leaves a wall far smaller than GoldSrc's trace epsilon.
    const Box thin_cut{{-1.99999999, -3, -3}, {3, 3, 3}};
    auto thin = carve_volume(solid, region, {&thin_cut, 1});
    close(measure(thin), 16e-8, 1e-14);
    hit = trace_volume(expand_volume(thin, point), {-4, 0, 0}, {4, 0, 0}, 0);
    close(hit.fraction, .25);
}

void validation() {
    const Box region{{-2, -2, -2}, {2, 2, 2}}, cut{{-1, -1, -1}, {1, 1, 1}};
    auto volume = carve_volume(solid, region, {&cut, 1});
    const double old_measure = measure(volume);
    rejects<std::length_error>([&] { volume = carve_volume(solid, region, {&cut, 1}, {1, 100000}); });
    close(measure(volume), old_measure);
    rejects<std::length_error>([&] { carve_volume(solid, region, {&cut, 1}, {4096, 2}); });
    rejects<std::invalid_argument>([&] { carve_volume(solid, region, {}, {0, 1000}); });
    rejects<std::invalid_argument>([&] { carve_volume(solid, point, {}); });
    auto invalid = cut;
    invalid.max[0] = std::numeric_limits<double>::quiet_NaN();
    rejects<std::invalid_argument>([&] { carve_volume(solid, region, {&invalid, 1}); });
    rejects<std::invalid_argument>([&] { expand_volume(volume, invalid); });
    rejects<std::length_error>([&] { expand_volume(volume, point, {1, 100000}); });
    rejects<std::length_error>([&] { expand_volume(volume, point, {4096, 2}); });
    const std::array<Plane, 1> planes{Plane{{1, 0, 0}, 0}};
    const std::array<HullNode, 1> cycle{HullNode{0, {0, -2}}}, missing{HullNode{1, {-1, -2}}};
    rejects<std::invalid_argument>([&] { carve_volume({planes, cycle, 0}, region, {}); });
    rejects<std::invalid_argument>([&] { carve_volume({planes, missing, 0}, region, {}); });
    rejects<std::invalid_argument>([&] { trace_volume({}, {}, {}, -1); });
    rejects<std::invalid_argument>(
        [&] { trace_volume({}, {}, {0, 0, std::numeric_limits<double>::infinity()}); });
    assert(expand_volume(carve_volume(empty, region, {}), point).cells.empty());
    auto invalid_volume = volume;
    invalid_volume.cells[0].faces[0].vertices.clear();
    rejects<std::invalid_argument>([&] { expand_volume(invalid_volume, point); });
    invalid_volume = volume;
    invalid_volume.cells[0].faces[0].plane.normal[0] = std::numeric_limits<double>::quiet_NaN();
    rejects<std::invalid_argument>([&] { expand_volume(invalid_volume, point); });
}
struct BoxHull {
    std::vector<Plane> planes;
    std::vector<HullNode> nodes;
    explicit BoxHull(std::span<const Box> boxes) {
        for(std::size_t b=0;b<boxes.size();++b)for(int axis=0;axis<3;++axis)for(int high=0;high<2;++high) {
            const auto index=static_cast<std::uint32_t>(planes.size());Point normal{};normal[axis]=high?1:-1;
            planes.push_back({normal,high?boxes[b].max[axis]:-boxes[b].min[axis]});
            const auto next_box=b+1==boxes.size()?-1:static_cast<int>((b+1)*6);
            nodes.push_back({index,{next_box,axis==2&&high?-2:static_cast<int>(index+1)}});
        }
    }
    HullView view() const { return {planes,nodes,0}; }
};
void native_overlay() {
    const Box wall{{-128,0,-128},{128,16,128}}, expanded{{-144,-16,-164},{144,32,164}};
    const Box clip_only{{-12,50,-96},{12,54,96}}, unrelated{{70,-80,-96},{90,-70,96}};
    const BoxHull point_source({&wall,1});
    const std::array<Box,3> obstacles{expanded,clip_only,unrelated};
    const BoxHull native(obstacles);
    const Box body{{-16,-16,-36},{16,16,36}};
    const std::array<Box,2> cuts{Box{{-32,-32,-48},{0,80,48}},Box{{0,-32,-48},{32,80,48}}};
    const EditedHull edit(point_source.view(),native.view(),body,cuts);
    auto hit=edit.trace({0,-64,0},{0,100,0},0);
    close(hit.fraction,114.0/164);
    close(hit.plane.normal[1],-1);
    assert(hit.in_open&&!hit.start_solid&&!hit.all_solid);
    assert(edit.contents({0,8,0})==-1);
    assert(edit.contents({0,52,0})==-2);
    assert(edit.contents({24,8,0})==-2);
    assert(!edit.affects({80,-100,0},{80,-65,0}));
    close(edit.trace({80,-100,0},{80,-65,0},0).fraction,20.0/35);
    // Native hull conventions win outside material-removal space, including
    // compiler-created empty space. Never add a new collision from the mesh.
    const EditedHull native_empty(point_source.view(),empty,body,cuts);
    assert(native_empty.contents({100,8,0})==-1);
    assert(native_empty.trace({100,-64,0},{100,64,0}).fraction==1);
    const EditedHull wet(point_source.view(),water,body,cuts);
    hit=wet.trace({0,-64,0},{0,64,0});
    assert(hit.in_water&&!hit.in_open&&hit.fraction==1);
    assert(wet.contents({0,8,0})==-3);
    const EditedHull first(point_source.view(),native.view(),body,{cuts.data(),1});
    assert(first.trace({0,-64,0},{0,64,0}).fraction<.5);
    const auto reversed=std::array<Box,2>{cuts[1],cuts[0]};
    const EditedHull reverse_edit(point_source.view(),native.view(),body,reversed);
    Random random;
    for(int i=0;i<20000;++i) {
        Point p{random()*96-48,random()*160-64,random()*96-48};
        bool old=false;
        for(const auto& b:obstacles)old|=p[0]>b.min[0]&&p[0]<b.max[0]&&p[1]>b.min[1]&&p[1]<b.max[1]&&p[2]>b.min[2]&&p[2]<b.max[2];
        const bool opened=std::abs(p[0])<16&&std::abs(p[2])<12&&p[1]>-16&&p[1]<32;
        assert((edit.contents(p)==-2)==(old&&!opened));
        assert(edit.contents(p)==reverse_edit.contents(p));
    }
    rejects<std::invalid_argument>([&]{EditedHull(point_source.view(),native.view(),body,{});});
    rejects<std::length_error>([&]{EditedHull(point_source.view(),native.view(),body,cuts,{4096,80});});
}
void indexed_queries() {
    std::vector<Box> boxes;
    for (int x = 0; x < 16; ++x)
        for (int y = 0; y < 4; ++y)
            boxes.push_back({{double(x * 16), double(y * 16), -4},
                             {double(x * 16 + 4), double(y * 16 + 4), 4}});
    const auto index = index_bounds(boxes);
    assert(index.nodes.size() == boxes.size() * 2 - 1);
    Random random;
    for (int i = 0; i < 10000; ++i) {
        Point a{random() * 256, random() * 64, random() * 16 - 8}, b = a;
        if (i % 2) b = {random() * 256, random() * 64, random() * 16 - 8};
        bool expected = false;
        for (const auto &box : boxes) {
            bool overlap = true;
            for (int axis = 0; axis < 3; ++axis)
                overlap &= std::max(a[axis], b[axis]) >= box.min[axis] &&
                           std::min(a[axis], b[axis]) <= box.max[axis];
            expected |= overlap;
        }
        assert(intersects(index, a, b) == expected);
    }
    assert(intersects(index, {4, 4, 4}, {4, 4, 4}));
    assert(!intersects(index, {std::nextafter(4.0, 5.0), 4, 4}, {5, 4, 4}));
    rejects<std::length_error>([&] { index_bounds(boxes, {32, 100000}); });
    rejects<std::length_error>([&] { index_bounds(boxes, {4096, 128}); });
    const auto exact_budget = index_bounds(boxes, {4096, index.operations});
    assert(exact_budget.nodes.size() == index.nodes.size());
    rejects<std::length_error>([&] { index_bounds(boxes, {4096, index.operations - 1}); });
    auto invalid = boxes;
    invalid[0].min[0] = std::numeric_limits<double>::quiet_NaN();
    rejects<std::invalid_argument>([&] { index_bounds(invalid); });
    assert(!intersects(index_bounds({}), {}, {}));

    const Box region{{-16, -16, -16}, {272, 80, 16}};
    auto collision = expand_volume(carve_volume(solid, region, boxes), point);
    const auto cells = index_volume(collision);
    std::vector<CollisionSpan> indexed;
    std::vector<std::size_t> candidates;
    for (int i = 0; i < 3000; ++i) {
        Point a{random() * 320 - 32, random() * 128 - 32, random() * 64 - 32};
        Point b{random() * 320 - 32, random() * 128 - 32, random() * 64 - 32};
        assert(contains(collision, a) == contains(collision, cells, a));
        const auto plain = volume_spans(collision, a, b);
        volume_spans(collision, a, b, indexed, candidates, &cells);
        assert(plain.size() == indexed.size());
        for (std::size_t span = 0; span < plain.size(); ++span) {
            assert(plain[span].begin == indexed[span].begin && plain[span].end == indexed[span].end);
            assert(plain[span].enter.normal == indexed[span].enter.normal &&
                   plain[span].enter.distance == indexed[span].enter.distance);
            assert(plain[span].leave.normal == indexed[span].leave.normal &&
                   plain[span].leave.distance == indexed[span].leave.distance);
        }
    }
    // The existing unindexed API remains mutable; it has no stale hidden cache.
    assert(contains(collision, {-8, 0, 0}));
    collision.cells.clear();
    assert(!contains(collision, {-8, 0, 0}));
    assert(volume_spans(collision, {-8, 0, 0}, {-8, 1, 0}).empty());
    const std::array<Box, 2> separated{Box{{-48, -8, -8}, {-32, 8, 8}},
                                     Box{{32, -8, -8}, {48, 8, 8}}};
    const EditedHull edit(solid, solid, point, separated);
    assert(!edit.affects({0, 0, 0}, {0, 1, 0}));
    assert(edit.contents({0, 0, 0}) == -2);
    assert(edit.affects({-32, 0, 0}, {-32, 1, 0}));

    const double n = std::sqrt(.5);
    const std::array<Plane, 1> diagonal{Plane{{n, n, 0}, -12}};
    const std::array<HullNode, 1> branch{HullNode{0, {-1, -2}}};
    const HullView slope{diagonal, branch, 0};
    const EditedHull sloped(slope, slope, point, separated);
    const Box slope_region{{-80, -80, -80}, {80, 80, 80}};
    for (Box body : {point, Box{{-16, -16, -36}, {16, 16, 36}}}) {
        const auto shaped = expand_volume(carve_volume(slope, slope_region, separated), body);
        const auto shaped_index = index_volume(shaped);
        for (int i = 0; i < 2000; ++i) {
            Point a{random() * 128 - 64, random() * 128 - 64, random() * 128 - 64};
            Point b{random() * 128 - 64, random() * 128 - 64, random() * 128 - 64};
            assert(contains(shaped, a) == contains(shaped, shaped_index, a));
            volume_spans(shaped, a, b, indexed, candidates, &shaped_index);
            const auto indexed_trace = trace_spans(indexed, a, b, contains(shaped, a), contains(shaped, b), .03125);
            const auto linear_trace = trace_volume(shaped, a, b);
            assert(indexed_trace.fraction == linear_trace.fraction && indexed_trace.plane.normal == linear_trace.plane.normal &&
                   indexed_trace.plane.distance == linear_trace.plane.distance && indexed_trace.start_solid == linear_trace.start_solid &&
                   indexed_trace.all_solid == linear_trace.all_solid);
            bool removed = false;
            for (const auto &hole : separated)
                removed |= a[0] > hole.min[0] && a[0] < hole.max[0] && a[1] > hole.min[1] &&
                           a[1] < hole.max[1] && a[2] > hole.min[2] && a[2] < hole.max[2];
            assert((sloped.contents(a) == -2) == (dot(a, diagonal[0].normal) < -12 && !removed));
        }
    }
    const EditedHull wet_point(slope, water, point, separated), clip_point(empty, solid, point, separated);
    assert(wet_point.contents({-40, 0, 0}) == -3);
    const auto water_trace = wet_point.trace({-60, 0, 0}, {60, 0, 0});
    assert(water_trace.in_water && !water_trace.in_open && water_trace.fraction == 1);
    assert(clip_point.contents({-40, 0, 0}) == -2);
    const auto clip_trace = clip_point.trace({-60, 0, 0}, {60, 0, 0});
    assert(clip_trace.start_solid && clip_trace.all_solid && !clip_trace.in_open && !clip_trace.in_water);
}
} // namespace

int main() {
    try {
        conservation();
        std::cout << "Conservation passed\n";
        openings();
        std::cout << "Openings passed\n";
        bevels();
        std::cout << "Bevels passed\n";
        traces();
        std::cout << "Traces passed\n";
        validation();
        std::cout << "Validation passed\n";
        native_overlay();
        indexed_queries();
        std::cout << "Prepared indices: 10000 independent box queries, 3000 exact volume queries, 4000 sloped point/body queries, boundary ties, water/clip preservation, bounded preparation and mutable volume API passed\n";
        std::cout << "Native overlay: 20000 oracle cases, retained clip-only obstacles, native empty/water, entrance and union traces passed\n";
        std::cout << "Carved volume: conservation/union/winding, 120000 analytic membership samples, "
                     "adjacent/low openings, asymmetric bodies, 3D bevels, shared-plane/solid traces and "
                     "atomic bounded failure passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
