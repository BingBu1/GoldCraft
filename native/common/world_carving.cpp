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

double dot(const Point& a, const Point& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void validate_contents(std::int32_t contents) {
    if (contents < -15 || contents > -1)
        throw std::invalid_argument("World carving BSP contents outside GoldSrc range");
}

std::size_t validate_node(HullView hull, std::int32_t node, std::vector<std::uint8_t>& state,
                          std::vector<std::size_t>& heights, std::size_t depth,
                          Result& work, const Limits& limits) {
    consume(work, limits, 1);
    if (node < 0) { validate_contents(node); return 0; }
    if (std::size_t(node) >= hull.nodes.size()) throw std::invalid_argument("World carving BSP child index");
    if (depth > 256) throw std::invalid_argument("World carving BSP depth exceeds 256");
    if (state[node] == 1) throw std::invalid_argument("World carving BSP cycle");
    if (state[node] == 2) return heights[node];
    const auto& branch = hull.nodes[node];
    if (branch.plane >= hull.planes.size()) throw std::invalid_argument("World carving BSP plane index");
    state[node] = 1;
    const auto front = validate_node(hull, branch.children[0], state, heights, depth + 1, work, limits);
    const auto back = validate_node(hull, branch.children[1], state, heights, depth + 1, work, limits);
    heights[node] = 1 + std::max(front, back);
    if (heights[node] > 256) throw std::invalid_argument("World carving BSP depth exceeds 256");
    state[node] = 2;
    return heights[node];
}

// Unlike an axial cut plane, a BSP plane can split a cavity face diagonally.
Split split_hull(const Polygon& polygon, const Plane& plane) {
    Split result;
    result.inside.reserve(polygon.size() + 1);
    result.outside.reserve(polygon.size() + 1);
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto& a = polygon[i];
        const auto& b = polygon[(i + 1) % polygon.size()];
        const double da = dot(a.position, plane.normal) - plane.distance;
        const double db = dot(b.position, plane.normal) - plane.distance;
        if (da <= 0) result.inside.push_back(a);
        if (da >= 0) result.outside.push_back(a);
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
            const double fraction = da / (da - db);
            Vertex crossing{};
            for (int axis = 0; axis < 3; ++axis)
                crossing.position[axis] = a.position[axis] + (b.position[axis] - a.position[axis]) * fraction;
            result.inside.push_back(crossing);
            result.outside.push_back(crossing);
        }
    }
    if (!has_area(result.inside)) result.inside.clear();
    if (!has_area(result.outside)) result.outside.clear();
    return result;
}

void clip_wall(HullView hull, std::int32_t node, const Polygon& polygon, const Point& normal,
               std::size_t box, Interior& output, Result& work, const Limits& limits) {
    if (polygon.empty()) return;
    consume(work, limits, polygon.size() + 1);
    if (node < 0) {
        if (node != -2) return;
        if (output.walls.size() >= limits.fragments)
            throw std::length_error("World carving interior fragment budget exceeded");
        Wall wall{{}, normal, box};
        wall.vertices.reserve(polygon.size());
        for (const auto& vertex : polygon) wall.vertices.push_back(vertex.position);
        output.walls.push_back(std::move(wall));
        return;
    }
    const auto& branch = hull.nodes[node];
    const auto& plane = hull.planes[branch.plane];
    double low = dot(polygon[0].position, plane.normal) - plane.distance, high = low;
    for (const auto& vertex : polygon) {
        const double distance = dot(vertex.position, plane.normal) - plane.distance;
        low = std::min(low, distance); high = std::max(high, distance);
    }
    if (low == 0 && high == 0) {
        // Classify the limiting point on the material side, without an epsilon
        // that might jump through a thin wall. The output normal faces the hole.
        const int side = dot(plane.normal, normal) > 0 ? 1 : 0;
        clip_wall(hull, branch.children[side], polygon, normal, box, output, work, limits);
    } else if (low >= 0) {
        clip_wall(hull, branch.children[0], polygon, normal, box, output, work, limits);
    } else if (high <= 0) {
        clip_wall(hull, branch.children[1], polygon, normal, box, output, work, limits);
    } else {
        const auto sides = split_hull(polygon, plane);
        clip_wall(hull, branch.children[0], sides.outside, normal, box, output, work, limits);
        clip_wall(hull, branch.children[1], sides.inside, normal, box, output, work, limits);
    }
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

Interior interior_walls(HullView hull, std::span<const Box> boxes, Limits limits) {
    if (!limits.fragments || !limits.operations)
        throw std::invalid_argument("World carving requires nonzero work limits");
    if (hull.nodes.size() > limits.operations || hull.planes.size() > limits.operations || boxes.size() > limits.operations)
        throw std::length_error("World carving input exceeds operation budget");
    Result work;
    for (const auto& plane : hull.planes) {
        consume(work, limits, 1);
        validate(plane.normal);
        if (!std::isfinite(plane.distance) || std::abs(plane.distance) > 1'000'000.0 ||
            std::abs(dot(plane.normal, plane.normal) - 1) > 0.02)
            throw std::invalid_argument("World carving BSP plane is not finite and normalized");
    }
    std::vector<std::uint8_t> state(hull.nodes.size());
    std::vector<std::size_t> heights(hull.nodes.size());
    validate_node(hull, hull.root, state, heights, 1, work, limits);
    for (const auto& box : boxes) {
        consume(work, limits, 1);
        validate(box.min); validate(box.max);
        for (int axis = 0; axis < 3; ++axis)
            if (box.min[axis] >= box.max[axis]) throw std::invalid_argument("World carving box has no volume");
    }
    Interior output;
    for (std::size_t current = 0; current < boxes.size(); ++current) {
        const auto& box = boxes[current];
        for (int axis = 0; axis < 3; ++axis) for (bool upper : {false, true}) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            const double face = upper ? box.max[axis] : box.min[axis];
            Polygon polygon(4);
            for (int vertex = 0; vertex < 4; ++vertex) {
                auto& point = polygon[vertex].position;
                point[axis] = face;
                point[u] = (vertex == 1 || vertex == 2) ? box.max[u] : box.min[u];
                point[v] = vertex >= 2 ? box.max[v] : box.min[v];
            }
            if (upper) std::reverse(polygon.begin(), polygon.end());
            Point normal{}; normal[axis] = upper ? -1.0 : 1.0;
            std::vector<Polygon> visible{std::move(polygon)};
            for (std::size_t other = 0; other < boxes.size() && !visible.empty(); ++other) {
                consume(work, limits, 1);
                if (other == current) continue;
                const auto& occluder = boxes[other];
                const bool outside = upper ? occluder.min[axis] <= face && occluder.max[axis] > face
                                           : occluder.max[axis] >= face && occluder.min[axis] < face;
                const bool shared_exterior = upper ? occluder.max[axis] == face : occluder.min[axis] == face;
                // When exterior faces coincide, the first box owns their shared
                // area. A touching neighbour on the other side always removes it.
                if (!outside && !(other < current && shared_exterior)) continue;
                std::vector<Polygon> next;
                for (const auto& piece : visible) {
                    consume(work, limits, piece.size());
                    if (overlaps(piece, occluder)) subtract_box(piece, occluder, next, work, limits);
                    else append(next, piece, limits);
                }
                visible = std::move(next);
            }
            for (const auto& piece : visible)
                clip_wall(hull, hull.root, piece, normal, current, output, work, limits);
        }
    }
    output.operations = work.operations;
    return output;
}

} // namespace goldcraft::carving
