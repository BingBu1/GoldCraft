#include "goldcraft/world_volume.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace goldcraft::carving {
namespace {
Point sub(Point a, const Point &b) {
    for (int i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
Point scale(Point a, double s) {
    for (auto &x : a)
        x *= s;
    return a;
}
double dot(const Point &a, const Point &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Point cross(const Point &a, const Point &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Plane reverse(Plane p) {
    p.normal = scale(p.normal, -1);
    p.distance = -p.distance;
    return p;
}
struct Work {
    Limits limits;
    std::size_t operations = 0;
    void use(std::size_t amount = 1) {
        if (amount > limits.operations - operations)
            throw std::length_error("Carved volume operation budget");
        operations += amount;
    }
    void cell(std::vector<Cell> &cells, Cell next) {
        if (next.faces.empty())
            return;
        if (cells.size() >= limits.fragments)
            throw std::length_error("Carved volume cell budget");
        cells.push_back(std::move(next));
    }
};
void validate(Point p) {
    for (auto x : p)
        if (!std::isfinite(x) || std::abs(x) > 1'000'000)
            throw std::invalid_argument("Carved volume coordinate");
}
void validate(Box b, bool point = false) {
    validate(b.min);
    validate(b.max);
    for (int i = 0; i < 3; ++i)
        if (point ? b.min[i] > b.max[i] : b.min[i] >= b.max[i])
            throw std::invalid_argument("Carved volume bounds");
}
Plane axial(int axis, bool upper, double value) {
    Point n{};
    n[axis] = upper ? 1.0 : -1.0;
    return {n, upper ? value : -value};
}
Cell cube(Box box) {
    Cell cell;
    for (int axis = 0; axis < 3; ++axis)
        for (bool upper : {false, true}) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            const double distance = upper ? box.max[axis] : box.min[axis];
            Face face{axial(axis, upper, distance), {}};
            for (int j = 0; j < 4; ++j) {
                Point p{};
                p[axis] = distance;
                p[u] = (j == 1 || j == 2) ? box.max[u] : box.min[u];
                p[v] = j >= 2 ? box.max[v] : box.min[v];
                face.vertices.push_back(p);
            }
            if (!upper)
                std::reverse(face.vertices.begin(), face.vertices.end());
            cell.faces.push_back(std::move(face));
        }
    return cell;
}
bool equal(const Point &a, const Point &b) {
    // Only merge roundoff duplicates. Do not use the much larger engine trace
    // epsilon here: that would delete thin walls before collision is compiled.
    for (int i = 0; i < 3; ++i)
        if (std::abs(a[i] - b[i]) >
            32 * std::numeric_limits<double>::epsilon() * std::max({1.0, std::abs(a[i]), std::abs(b[i])}))
            return false;
    return true;
}
double signed_distance(const Point &point, const Plane &plane) {
    const double d = dot(point, plane.normal) - plane.distance;
    double magnitude = std::abs(plane.distance);
    for (int i = 0; i < 3; ++i)
        magnitude += std::abs(point[i] * plane.normal[i]);
    // A bound on arithmetic roundoff, not an engine collision epsilon. Repeated
    // clipping must not turn a coplanar face into a zero-volume solid cell.
    return std::abs(d) <= 16 * std::numeric_limits<double>::epsilon() * std::max(1.0, magnitude) ? 0 : d;
}
bool area(const std::vector<Point> &points) {
    if (points.size() < 3)
        return false;
    Point n{};
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        const auto t = cross(sub(points[i], points[0]), sub(points[i + 1], points[0]));
        for (int j = 0; j < 3; ++j)
            n[j] += t[j];
    }
    return dot(n, n) > 1e-28;
}
void unique(std::vector<Point> &points, Point p, Work &work) {
    work.use(points.size() + 1);
    if (std::ranges::none_of(points, [&](const Point &q) { return equal(p, q); }))
        points.push_back(p);
}
// Keep the negative side. Every plane creates at most one convex cap.
Cell clip(const Cell &input, const Plane &plane, Work &work) {
    bool negative = false, positive = false;
    for (const auto &f : input.faces)
        for (const auto &p : f.vertices) {
            work.use();
            const double d = signed_distance(p, plane);
            negative |= d < 0;
            positive |= d > 0;
        }
    if (!positive)
        return input;
    if (!negative)
        return {};
    Cell output;
    std::vector<Point> cap;
    for (const auto &source : input.faces) {
        Face face{source.plane, {}};
        for (std::size_t i = 0; i < source.vertices.size(); ++i) {
            work.use();
            const auto &a = source.vertices[i];
            const auto &b = source.vertices[(i + 1) % source.vertices.size()];
            const double da = signed_distance(a, plane), db = signed_distance(b, plane);
            if (da <= 0)
                face.vertices.push_back(a);
            if (da == 0)
                unique(cap, a, work);
            if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
                Point p{};
                const double t = da / (da - db);
                for (int axis = 0; axis < 3; ++axis)
                    p[axis] = a[axis] + (b[axis] - a[axis]) * t;
                // Make axial cuts exact; on oblique planes solve the best
                // conditioned coordinate instead of keeping interpolation drift.
                const auto axis = std::size_t(
                    std::max_element(plane.normal.begin(), plane.normal.end(),
                                     [](double x, double y) { return std::abs(x) < std::abs(y); }) -
                    plane.normal.begin());
                p[axis] = 0;
                p[axis] = (plane.distance - dot(p, plane.normal)) / plane.normal[axis];
                face.vertices.push_back(p);
                unique(cap, p, work);
            }
        }
        if (area(face.vertices))
            output.faces.push_back(std::move(face));
    }
    if (cap.size() < 3) {
        throw std::runtime_error("Carved volume cap lost precision: points=" + std::to_string(cap.size()) +
                                 " plane=" + std::to_string(plane.normal[0]) + "," +
                                 std::to_string(plane.normal[1]) + "," + std::to_string(plane.normal[2]) +
                                 "," + std::to_string(plane.distance));
    }
    Point center{};
    for (const auto &p : cap)
        for (int i = 0; i < 3; ++i)
            center[i] += p[i] / cap.size();
    const int axis =
        static_cast<int>(std::min_element(plane.normal.begin(), plane.normal.end(),
                                          [](double a, double b) { return std::abs(a) < std::abs(b); }) -
                         plane.normal.begin());
    Point seed{};
    seed[axis] = 1;
    const auto u = cross(seed, plane.normal), v = cross(plane.normal, u);
    work.use(cap.size() * cap.size());
    std::sort(cap.begin(), cap.end(), [&](const Point &a, const Point &b) {
        const auto x = sub(a, center), y = sub(b, center);
        return std::atan2(dot(x, v), dot(x, u)) < std::atan2(dot(y, v), dot(y, u));
    });
    if (!area(cap))
        throw std::runtime_error("Carved volume cap has no area");
    output.faces.push_back({plane, std::move(cap)});
    return output;
}
void collect(HullView hull, std::int32_t node, Cell cell, Work &work, std::vector<Cell> &output) {
    if (cell.faces.empty())
        return;
    work.use();
    if (node < 0) {
        if (node == -2)
            work.cell(output, std::move(cell));
        return;
    }
    const auto &branch = hull.nodes[node];
    const auto &plane = hull.planes[branch.plane];
    collect(hull, branch.children[0], clip(cell, reverse(plane), work), work, output);
    collect(hull, branch.children[1], clip(cell, plane, work), work, output);
}
Box bounds(const Cell &cell) {
    Box b{cell.faces.front().vertices.front(), cell.faces.front().vertices.front()};
    for (const auto &f : cell.faces)
        for (const auto &p : f.vertices)
            for (int i = 0; i < 3; ++i) {
                b.min[i] = std::min(b.min[i], p[i]);
                b.max[i] = std::max(b.max[i], p[i]);
            }
    return b;
}
bool overlap(Box a, Box b) {
    for (int i = 0; i < 3; ++i)
        if (a.max[i] <= b.min[i] || a.min[i] >= b.max[i])
            return false;
    return true;
}
void subtract_cell(const Cell &cell, const Box &box, Work &work, std::vector<Cell> &output) {
    if (!overlap(bounds(cell), box)) {
        work.cell(output, cell);
        return;
    }
    Cell remaining = cell;
    std::vector<Cell> pieces;
    for (int axis = 0; axis < 3; ++axis)
        for (bool upper : {false, true}) {
            const auto p = axial(axis, upper, upper ? box.max[axis] : box.min[axis]);
            auto inside = clip(remaining, p, work);
            if (inside.faces.empty()) {
                work.cell(output, cell);
                return;
            }
            work.cell(pieces, clip(remaining, reverse(p), work));
            remaining = std::move(inside);
        }
    for (auto &piece : pieces)
        work.cell(output, std::move(piece));
}
bool negative_side(const Plane &plane, double distance) {
    if (distance != 0)
        return distance < 0;
    // Opposite halfspaces give shared boundaries to exactly one side. This
    // prevents both adjacent cells excluding a ray lying on their partition.
    for (auto n : plane.normal)
        if (n != 0)
            return n < 0;
    return false;
}
bool occupied(const CollisionCell &cell, const Point &point) {
    for (int i = 0; i < 3; ++i)
        if (point[i] < cell.bounds.min[i] || point[i] > cell.bounds.max[i])
            return false;
    return std::ranges::all_of(
        cell.planes, [&](const Plane &p) { return negative_side(p, dot(point, p.normal) - p.distance); });
}
} // namespace

Volume carve_volume(HullView hull, Box region, std::span<const Box> cuts, Limits limits) {
    validate(region);
    Work work{limits};
    work.operations = validate_hull(hull, limits);
    if (cuts.size() > limits.operations)
        throw std::length_error("Carved volume cut budget");
    for (const auto &b : cuts) {
        validate(b);
        work.use();
    }
    Volume result{region, {}};
    collect(hull, hull.root, cube(region), work, result.cells);
    for (const auto &cut : cuts) {
        std::vector<Cell> next;
        for (const auto &cell : result.cells) {
            work.use();
            subtract_cell(cell, cut, work, next);
        }
        result.cells = std::move(next);
    }
    result.operations = work.operations;
    return result;
}

CollisionVolume expand_volume(const Volume &volume, Box body, Limits limits) {
    validate(body, true);
    validate(volume.region);
    if (!limits.operations || !limits.fragments)
        throw std::invalid_argument("Carved collision limits");
    if (volume.cells.size() > limits.fragments)
        throw std::length_error("Carved collision cell budget");
    Work work{limits};
    CollisionVolume result;
    for (const auto &cell : volume.cells) {
        if (cell.faces.size() < 4)
            throw std::invalid_argument("Incomplete convex cell");
        for (const auto &face : cell.faces) {
            work.use();
            validate(face.plane.normal);
            if (face.vertices.size() < 3 || !std::isfinite(face.plane.distance) ||
                std::abs(dot(face.plane.normal, face.plane.normal) - 1) > 0.02)
                throw std::invalid_argument("Invalid convex face");
        }
        CollisionCell expanded{};
        const auto b = bounds(cell);
        for (int i = 0; i < 3; ++i) {
            expanded.bounds.min[i] = b.min[i] - body.max[i];
            expanded.bounds.max[i] = b.max[i] - body.min[i];
        }
        std::vector<Point> points;
        for (const auto &f : cell.faces)
            for (const auto &p : f.vertices) {
                validate(p);
                unique(points, p, work);
            }
        const auto add_plane = [&](Point normal) {
            work.use();
            const double length = std::sqrt(dot(normal, normal));
            if (length < 1e-12)
                return;
            normal = scale(normal, 1 / length);
            work.use(expanded.planes.size());
            if (std::ranges::any_of(expanded.planes, [&](const Plane &p) { return equal(p.normal, normal); }))
                return;
            double d = -std::numeric_limits<double>::infinity();
            work.use(points.size());
            for (const auto &p : points)
                d = std::max(d, dot(p, normal));
            for (int i = 0; i < 3; ++i)
                d -= normal[i] * (normal[i] >= 0 ? body.min[i] : body.max[i]);
            expanded.planes.push_back({normal, d});
        };
        for (int axis = 0; axis < 3; ++axis) {
            Point n{};
            n[axis] = 1;
            add_plane(n);
            add_plane(scale(n, -1));
        }
        for (const auto &f : cell.faces) {
            add_plane(f.plane.normal);
            for (std::size_t i = 0; i < f.vertices.size(); ++i) {
                const auto edge = sub(f.vertices[(i + 1) % f.vertices.size()], f.vertices[i]);
                for (int axis = 0; axis < 3; ++axis) {
                    Point n{};
                    n[axis] = 1;
                    const auto bevel = cross(edge, n);
                    add_plane(bevel);
                    add_plane(scale(bevel, -1));
                }
            }
        }
        if (result.cells.size() >= limits.fragments)
            throw std::length_error("Carved collision cell budget");
        result.cells.push_back(std::move(expanded));
    }
    result.operations = work.operations;
    return result;
}
bool contains(const CollisionVolume &volume, Point point) {
    validate(point);
    return std::ranges::any_of(volume.cells,
                               [&](const CollisionCell &cell) { return occupied(cell, point); });
}
VolumeTrace trace_volume(const CollisionVolume &volume, Point start, Point end, double margin) {
    validate(start);
    validate(end);
    if (!std::isfinite(margin) || margin < 0 || margin > 1)
        throw std::invalid_argument("Carved collision margin");
    struct Interval {
        double begin, end;
        Plane plane;
    };
    std::vector<Interval> intervals;
    const auto move = sub(end, start);
    VolumeTrace result;
    result.start_solid = contains(volume, start);
    for (const auto &cell : volume.cells) {
        double enter = -std::numeric_limits<double>::infinity(),
               exit = std::numeric_limits<double>::infinity();
        Plane plane{};
        bool miss = false;
        for (int axis = 0; axis < 3; ++axis)
            if (std::max(start[axis], end[axis]) < cell.bounds.min[axis] ||
                std::min(start[axis], end[axis]) > cell.bounds.max[axis])
                miss = true;
        if (miss)
            continue;
        for (const auto &p : cell.planes) {
            const double distance = dot(start, p.normal) - p.distance, speed = dot(move, p.normal);
            if (speed == 0) {
                if (!negative_side(p, distance)) {
                    miss = true;
                    break;
                }
                continue;
            }
            const double t = -distance / speed;
            if (speed < 0) {
                if (t > enter) {
                    enter = t;
                    plane = p;
                }
            } else
                exit = std::min(exit, t);
            if (enter >= exit) {
                miss = true;
                break;
            }
        }
        if (!miss && enter < 1 && exit > 0)
            intervals.push_back({enter, exit, plane});
    }
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval &a, const Interval &b) { return a.begin < b.begin; });
    std::vector<Interval> merged;
    for (const auto &interval : intervals) {
        if (merged.empty() || interval.begin > merged.back().end)
            merged.push_back(interval);
        else
            merged.back().end = std::max(merged.back().end, interval.end);
    }
    result.all_solid =
        result.start_solid && contains(volume, end) && !merged.empty() && merged.front().end >= 1;
    for (const auto &interval : merged) {
        if (interval.begin < 0 || (interval.begin == 0 && result.start_solid))
            continue;
        const double speed = -dot(move, interval.plane.normal);
        result.fraction = std::clamp(interval.begin - (speed > 0 ? margin / speed : 0), 0.0, 1.0);
        result.plane = interval.plane;
        break;
    }
    return result;
}
} // namespace goldcraft::carving
