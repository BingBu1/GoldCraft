#include "goldcraft/world_carving.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace goldcraft::carving {
namespace {

void validate(const Point& point) {
    for (double coordinate : point)
        if (!std::isfinite(coordinate) || std::abs(coordinate) > 1'000'000.0)
            throw std::invalid_argument("World carving coordinate outside supported bounds");
}

bool has_area(const Polygon& polygon) {
    if (polygon.size() < 3) return false;
    Point normal{};
    const Point& origin = polygon[0].position;
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
        Point a{}, b{};
        for (int axis = 0; axis < 3; ++axis) {
            a[axis] = polygon[i].position[axis] - origin[axis];
            b[axis] = polygon[i + 1].position[axis] - origin[axis];
        }
        normal[0] += a[1] * b[2] - a[2] * b[1];
        normal[1] += a[2] * b[0] - a[0] * b[2];
        normal[2] += a[0] * b[1] - a[1] * b[0];
    }
    return normal[0] != 0 || normal[1] != 0 || normal[2] != 0;
}

void consume(Result& result, const Limits& limits, std::size_t count) {
    if (count > limits.operations - result.operations)
        throw std::length_error("World carving operation budget exceeded");
    result.operations += count;
}

bool overlaps(const Polygon& polygon, const Box& box) {
    for (int axis = 0; axis < 3; ++axis) {
        double lo = polygon[0].position[axis], hi = lo;
        for (const auto& vertex : polygon) {
            lo = std::min(lo, vertex.position[axis]);
            hi = std::max(hi, vertex.position[axis]);
        }
        if (hi < box.min[axis] || lo > box.max[axis]) return false;
    }
    return true;
}

struct Split { Polygon inside, outside; };

Split split(const Polygon& polygon, int axis, double plane, bool lower) {
    Split result;
    result.inside.reserve(polygon.size() + 1);
    result.outside.reserve(polygon.size() + 1);
    bool has_outside = false;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vertex& a = polygon[i];
        const Vertex& b = polygon[(i + 1) % polygon.size()];
        double da = a.position[axis] - plane;
        double db = b.position[axis] - plane;
        if (lower) { da = -da; db = -db; }
        if (da <= 0) result.inside.push_back(a);
        if (da >= 0) result.outside.push_back(a);
        has_outside |= da > 0;
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
            const double fraction = da / (da - db);
            Vertex crossing{};
            for (int component = 0; component < 3; ++component) {
                crossing.position[component] = a.position[component] +
                    (b.position[component] - a.position[component]) * fraction;
                crossing.weights[component] = a.weights[component] +
                    (b.weights[component] - a.weights[component]) * fraction;
            }
            crossing.position[axis] = plane;
            result.inside.push_back(crossing);
            result.outside.push_back(crossing);
        }
    }
    // A face exactly on the cutting plane is inside, not duplicated outside.
    if (!has_outside || !has_area(result.outside)) result.outside.clear();
    if (!has_area(result.inside)) result.inside.clear();
    return result;
}

void append(std::vector<Polygon>& destination, Polygon polygon, const Limits& limits) {
    if (polygon.empty()) return;
    if (destination.size() >= limits.fragments)
        throw std::length_error("World carving fragment budget exceeded");
    destination.push_back(std::move(polygon));
}

bool subtract_box(const Polygon& input, const Box& box, std::vector<Polygon>& output,
                  Result& result, const Limits& limits) {
    Polygon remaining = input;
    std::vector<Polygon> outside;
    for (int axis = 0; axis < 3; ++axis) {
        for (bool lower : {true, false}) {
            consume(result, limits, remaining.size());
            auto sides = split(remaining, axis, lower ? box.min[axis] : box.max[axis], lower);
            if (sides.inside.empty()) {
                // Broad bounds overlap can still miss an angled triangle. Keep
                // its original vertices rather than unnecessary subdivisions.
                append(output, input, limits);
                return false;
            }
            if (!sides.outside.empty()) outside.push_back(std::move(sides.outside));
            remaining = std::move(sides.inside);
        }
    }
    for (auto& polygon : outside) append(output, std::move(polygon), limits);
    return true;
}

} // namespace

Result subtract(const Triangle& triangle, std::span<const Box> boxes, Limits limits) {
    if (!limits.fragments || !limits.operations)
        throw std::invalid_argument("World carving requires nonzero work limits");
    if (boxes.size() > limits.operations)
        throw std::length_error("World carving box count exceeds operation budget");
    for (const auto& point : triangle) validate(point);
    // Validate the entire request before constructing any candidate geometry.
    for (const auto& box : boxes) {
        validate(box.min); validate(box.max);
        for (int axis = 0; axis < 3; ++axis)
            if (box.min[axis] >= box.max[axis])
                throw std::invalid_argument("World carving box has no volume");
    }
    Result result;
    Polygon initial;
    for (std::size_t i = 0; i < triangle.size(); ++i) {
        Vertex vertex{triangle[i], {}};
        vertex.weights[i] = 1;
        initial.push_back(vertex);
    }
    if (!has_area(initial)) return result;
    result.pieces.push_back(std::move(initial));
    for (const auto& box : boxes) {
        consume(result, limits, 1);
        std::vector<Polygon> next;
        bool changed = false;
        for (const auto& polygon : result.pieces) {
            consume(result, limits, polygon.size());
            if (!overlaps(polygon, box)) append(next, polygon, limits);
            else changed |= subtract_box(polygon, box, next, result, limits);
        }
        if (changed) {
            result.pieces = std::move(next);
            result.changed = true;
        }
        if (result.pieces.empty()) break;
    }
    return result;
}

} // namespace goldcraft::carving
