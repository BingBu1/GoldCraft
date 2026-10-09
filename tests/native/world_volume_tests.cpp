#include "goldcraft/world_volume.hpp"

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
        std::cout << "Carved volume: conservation/union/winding, 120000 analytic membership samples, "
                     "adjacent/low openings, asymmetric bodies, 3D bevels, shared-plane/solid traces and "
                     "atomic bounded failure passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
