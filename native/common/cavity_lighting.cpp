#include "goldcraft/cavity_lighting.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace goldcraft::lighting {
namespace {
using carving::Point;
Point sub(const Point &a, const Point &b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
double dot(const Point &a, const Point &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Point point(const float (&p)[3]) { return {p[0], p[1], p[2]}; }
void valid(const Point &p) {
    for (double v : p) if (!std::isfinite(v) || std::abs(v) > 1'000'000)
        throw std::invalid_argument("Cavity lighting coordinate");
}
void consume(std::size_t &work, std::size_t limit, std::size_t n = 1) {
    if (work > limit || n > limit-work) throw std::length_error("Cavity lighting work budget");
    work += n;
}
struct Interval { double begin, end; };
struct Visibility {
    carving::HullView hull;
    Point start, delta;
    std::vector<Interval> openings;
    std::size_t &work;
    std::size_t limit;
    bool covered(double a, double b) const {
        const auto it = std::lower_bound(openings.begin(), openings.end(), a,
            [](const Interval &part, double value) { return part.end < value; });
        return it != openings.end() && it->begin <= a && it->end >= b;
    }
    bool visit(std::int32_t node, double a, double b, unsigned depth = 0) {
        consume(work, limit);
        if (a >= b || covered(a, b)) return true;
        if (node < 0) return node != -2;
        if (depth >= 256 || std::size_t(node) >= hull.nodes.size())
            throw std::invalid_argument("Cavity lighting BSP node");
        const auto &branch = hull.nodes[node];
        if (branch.plane >= hull.planes.size()) throw std::invalid_argument("Cavity lighting BSP plane");
        const auto &plane = hull.planes[branch.plane];
        const double distance = dot(start, plane.normal)-plane.distance, speed = dot(delta, plane.normal);
        const double da = distance+speed*a, db = distance+speed*b;
        if (da >= 0 && db >= 0) return visit(branch.children[0], a, b, depth+1);
        if (da <= 0 && db <= 0) return visit(branch.children[1], a, b, depth+1);
        const double split = std::clamp(-distance/speed, a, b);
        const unsigned near = speed > 0 ? 1 : 0;
        return visit(branch.children[near], a, split, depth+1) && visit(branch.children[near^1], split, b, depth+1);
    }
};
struct Candidate {
    double distance;
    std::uint32_t probe;
    bool operator<(const Candidate &other) const {
        return distance != other.distance ? distance < other.distance : probe < other.probe;
    }
};
// Median tree over immutable map probes; a query only visits nearby partitions.
// The heap is reused for every sample and never appears in the drawing path.
class Field {
  public:
    Field(const Model &model, const Settings &settings, std::size_t &work)
        : model_(model), settings_(settings), work_(work), order_(model.probes.size()) {
        if (order_.empty()) throw std::invalid_argument("Missing baked map lighting probes");
        if (order_.size() > settings.probes || model.values.size() > settings.values)
            throw std::length_error("Cavity lighting probe capacity");
        for (const auto &value : model.values) {
            consume(work_, settings_.mesh.operations);
            if (value.style >= 255) throw std::invalid_argument("Cavity lighting style");
            for (float c : value.rgb) if (!std::isfinite(c) || c < 0 || c > 1)
                throw std::invalid_argument("Cavity lighting coefficient");
        }
        for (const auto &probe : model.probes) {
            consume(work_, settings_.mesh.operations);
            valid(probe.position);
            if (!probe.values.count || probe.values.first > model.values.size() ||
                probe.values.count > model.values.size()-probe.values.first)
                throw std::invalid_argument("Cavity lighting probe range");
            std::array<bool, 255> styles{};
            for (std::size_t i = probe.values.first; i < std::size_t(probe.values.first)+probe.values.count; ++i) {
                consume(work_, settings_.mesh.operations);
                if (styles[model.values[i].style]) throw std::invalid_argument("Duplicate probe light style");
                styles[model.values[i].style] = true;
            }
        }
        std::iota(order_.begin(), order_.end(), 0u);
        split(0, order_.size(), 0);
        heap_.reserve(settings.neighbours);
    }
    surface::Range sample(Point p, Point normal, Prepared &result) {
        valid(p); valid(normal);
        if (std::abs(dot(normal, normal)-1) > 1e-4) throw std::invalid_argument("Cavity lighting normal");
        heap_.clear();
        query(0, order_.size(), 0, p, normal);
        // Stable summation, independent of the median partition/heap order.
        std::sort(heap_.begin(), heap_.end());
        std::array<Point, 255> coefficients{};
        double total = 0;
        for (const auto &candidate : heap_) {
            consume(work_, settings_.mesh.operations);
            const auto &probe = model_.probes[candidate.probe];
            const auto delta = sub(probe.position, p);
            const auto cosine = dot(delta, normal)/std::sqrt(candidate.distance);
            const auto radial = 1-candidate.distance/(settings_.radius*settings_.radius);
            const auto weight = cosine*radial*radial/(1+candidate.distance);
            total += weight;
            ++result.rays;
            if (!visible(model_.hull, model_.cuts, p, probe.position, work_, settings_.mesh.operations)) {
                ++result.occluded;
                continue;
            }
            for (std::size_t i = probe.values.first; i < std::size_t(probe.values.first)+probe.values.count; ++i) {
                consume(work_, settings_.mesh.operations);
                const auto &value = model_.values[i];
                for (int axis = 0; axis < 3; ++axis) coefficients[value.style][axis] += value.rgb[axis]*weight;
            }
        }
        surface::Range range{static_cast<std::uint32_t>(result.values.size()), 0};
        if (total > 0) for (std::uint32_t style = 0; style < 255; ++style) {
            const auto &rgb = coefficients[style];
            if (rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0) continue;
            if (result.values.size() >= settings_.values) throw std::length_error("Cavity light buffer capacity");
            result.values.push_back({{static_cast<float>(rgb[0]/total), static_cast<float>(rgb[1]/total),
                                      static_cast<float>(rgb[2]/total)}, style});
            ++range.count;
        }
        ++result.samples;
        return range;
    }
  private:
    void split(std::size_t begin, std::size_t end, unsigned depth) {
        if (begin == end) return;
        consume(work_, settings_.mesh.operations, end-begin);
        const auto mid = begin+(end-begin)/2, axis = std::size_t(depth%3);
        std::nth_element(order_.begin()+begin, order_.begin()+mid, order_.begin()+end,
            [&](auto a, auto b) {
                const auto pa = model_.probes[a].position[axis], pb = model_.probes[b].position[axis];
                return pa != pb ? pa < pb : a < b;
            });
        split(begin, mid, depth+1); split(mid+1, end, depth+1);
    }
    void query(std::size_t begin, std::size_t end, unsigned depth, Point p, Point normal) {
        if (begin == end) return;
        consume(work_, settings_.mesh.operations);
        const auto mid = begin+(end-begin)/2, index = order_[mid];
        const auto delta = sub(model_.probes[index].position, p);
        const auto distance = dot(delta, delta);
        const Candidate candidate{distance, index};
        if (distance > 0 && distance < settings_.radius*settings_.radius && dot(delta, normal) > 0) {
            if (heap_.size() < settings_.neighbours) {
                heap_.push_back(candidate); std::push_heap(heap_.begin(), heap_.end());
            } else if (candidate < heap_.front()) {
                std::pop_heap(heap_.begin(), heap_.end()); heap_.back() = candidate;
                std::push_heap(heap_.begin(), heap_.end());
            }
        }
        const auto side = delta[depth%3];
        if (side >= 0) query(begin, mid, depth+1, p, normal);
        else query(mid+1, end, depth+1, p, normal);
        const auto range = heap_.size() == settings_.neighbours ? heap_.front().distance : settings_.radius*settings_.radius;
        if (side*side <= range) {
            if (side >= 0) query(mid+1, end, depth+1, p, normal);
            else query(begin, mid, depth+1, p, normal);
        }
    }
    const Model &model_;
    const Settings &settings_;
    std::size_t &work_;
    std::vector<std::uint32_t> order_;
    std::vector<Candidate> heap_;
};
using Key = std::array<std::uint32_t, 6>;
struct Hash { std::size_t operator()(const Key &key) const {
    std::size_t result = 2166136261u;
    for (auto v : key) result = (result^v)*16777619u;
    return result;
}};
Key key(const surface::Vertex &v, const surface::Frame &frame) {
    Key result{};
    for (int i = 0; i < 3; ++i) {
        result[i] = std::bit_cast<std::uint32_t>(v.pos[i] == 0 ? 0.f : v.pos[i]);
        result[i+3] = std::bit_cast<std::uint32_t>(frame.normal[i] == 0 ? 0.f : frame.normal[i]);
    }
    return result;
}
} // namespace

bool visible(carving::HullView hull, std::span<const carving::Box> cuts, Point start, Point end,
             std::size_t &operations, std::size_t limit) {
    valid(start); valid(end);
    Visibility view{hull, start, sub(end, start), {}, operations, limit};
    consume(operations, limit, cuts.size());
    for (const auto &cut : cuts) {
        Interval part{0, 1};
        for (int axis = 0; axis < 3; ++axis) {
            if (view.delta[axis] == 0) {
                if (start[axis] < cut.min[axis] || start[axis] > cut.max[axis]) { part.end = -1; break; }
            } else {
                const auto a = (cut.min[axis]-start[axis])/view.delta[axis];
                const auto b = (cut.max[axis]-start[axis])/view.delta[axis];
                part.begin = std::max(part.begin, std::min(a,b));
                part.end = std::min(part.end, std::max(a,b));
            }
        }
        if (part.begin < part.end) view.openings.push_back(part);
    }
    consume(operations, limit, view.openings.size()*std::max<std::size_t>(1, std::bit_width(view.openings.size())));
    std::sort(view.openings.begin(), view.openings.end(), [](auto a, auto b) { return a.begin < b.begin; });
    std::size_t kept = 0;
    for (auto part : view.openings) {
        if (kept && view.openings[kept-1].end >= part.begin)
            view.openings[kept-1].end = std::max(view.openings[kept-1].end, part.end);
        else view.openings[kept++] = part;
    }
    view.openings.resize(kept);
    return view.visit(hull.root, 0, 1);
}

Prepared prepare(const cavity::Prepared &geometry, std::span<const Model> models, Settings settings) {
    if (!geometry.mesh || !std::isfinite(settings.radius) || settings.radius <= 0 || settings.radius > 65536 ||
        !std::isfinite(settings.spacing) || settings.spacing <= 0 || !settings.neighbours || settings.neighbours > 256 ||
        settings.values > std::numeric_limits<std::uint32_t>::max() ||
        settings.mesh.vertices > std::numeric_limits<std::uint32_t>::max() ||
        settings.mesh.indices > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Cavity lighting settings");
    Prepared result{geometry.mesh, geometry.draws, {}, {}, 0, 0, 0, 0};
    if (geometry.draws.empty()) return result;
    const auto &source = *geometry.mesh;
    if (source.vertices.size() != source.frames.size() || source.vertices.size() > settings.mesh.vertices ||
        source.indices.size() > settings.mesh.indices) throw std::length_error("Cavity lighting mesh capacity");
    auto mesh = std::make_shared<surface::Mesh>(source);
    result.vertices.resize(mesh->vertices.size());
    std::vector<bool> handled(result.draws.size());
    for (const auto &model : models) {
        consume(result.operations, settings.mesh.operations, carving::validate_hull(model.hull,
            {settings.mesh.fragments, settings.mesh.operations-result.operations}));
        for (const auto &cut : model.cuts) {
            valid(cut.min); valid(cut.max);
            for (int i = 0; i < 3; ++i) if (cut.min[i] >= cut.max[i]) throw std::invalid_argument("Cavity lighting cut");
        }
        Field field(model, settings, result.operations);
        std::unordered_map<Key, surface::Range, Hash> cache;
        for (std::size_t d = 0; d < result.draws.size(); ++d) {
            auto &draw = result.draws[d];
            if (draw.first_face != model.first_face || draw.face_count != model.face_count) continue;
            if (handled[d]) throw std::invalid_argument("Duplicate cavity lighting model");
            handled[d] = true;
            const auto old = draw.indices;
            if (old.first > source.indices.size() || old.count > source.indices.size()-old.first || old.count%3)
                throw std::invalid_argument("Cavity lighting draw range");
            draw.indices = {static_cast<std::uint32_t>(mesh->indices.size()), 0};
            for (std::size_t at = old.first; at < std::size_t(old.first)+old.count; at += 3) {
                std::array<surface::Vertex, 3> triangle;
                for (int i = 0; i < 3; ++i) {
                    if (source.indices[at+i] >= source.vertices.size()) throw std::invalid_argument("Cavity lighting vertex");
                    triangle[i] = source.vertices[source.indices[at+i]];
                }
                const auto &frame = source.frames[source.indices[at]];
                double length = 0;
                for (int i = 0; i < 3; ++i) {
                    const auto edge = sub(point(triangle[i].pos), point(triangle[(i+1)%3].pos));
                    length = std::max(length, std::sqrt(dot(edge, edge)));
                }
                const auto steps = std::max(1., std::ceil(length/settings.spacing));
                if (!std::isfinite(steps) || steps > 1024) throw std::length_error("Cavity lighting subdivision budget");
                const auto n = static_cast<std::uint32_t>(steps);
                const auto count = std::size_t(n+1)*(n+2)/2;
                if (count > settings.mesh.vertices-mesh->vertices.size() ||
                    std::size_t(n)*n*3 > settings.mesh.indices-mesh->indices.size())
                    throw std::length_error("Cavity lighting refinement capacity");
                consume(result.operations, settings.mesh.operations, count+std::size_t(n)*n*3);
                const auto base = static_cast<std::uint32_t>(mesh->vertices.size());
                for (std::uint32_t i = 0; i <= n; ++i) for (std::uint32_t j = 0; j <= n-i; ++j) {
                    const double weights[]{double(n-i-j)/n, double(i)/n, double(j)/n};
                    surface::Vertex vertex{};
                    for (int axis = 0; axis < 3; ++axis) {
                        double coordinate = 0;
                        for (int k = 0; k < 3; ++k) coordinate += weights[k]*triangle[k].pos[axis];
                        vertex.pos[axis] = static_cast<float>(coordinate);
                    }
                    for (int axis = 0; axis < 2; ++axis) {
                        double uv = 0, lm = 0;
                        for (int k = 0; k < 3; ++k) { uv += weights[k]*triangle[k].texcoord[axis]; lm += weights[k]*triangle[k].lightmaptexcoord[axis]; }
                        vertex.texcoord[axis] = static_cast<float>(uv); vertex.lightmaptexcoord[axis] = static_cast<float>(lm);
                    }
                    const auto sample_key = key(vertex, frame);
                    auto it = cache.find(sample_key);
                    if (it == cache.end()) it = cache.emplace(sample_key, field.sample(point(vertex.pos), point(frame.normal), result)).first;
                    mesh->vertices.push_back(vertex); mesh->frames.push_back(frame); result.vertices.push_back(it->second);
                    for (int axis = 0; axis < 3; ++axis) {
                        draw.bounds.min[axis] = std::min(draw.bounds.min[axis], double(vertex.pos[axis]));
                        draw.bounds.max[axis] = std::max(draw.bounds.max[axis], double(vertex.pos[axis]));
                    }
                }
                const auto row = [n, base](std::uint32_t i, std::uint32_t j) { return base+i*(2*n+3-i)/2+j; };
                const auto emit = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                    const auto ab = sub(point(mesh->vertices[b].pos), point(mesh->vertices[a].pos));
                    const auto ac = sub(point(mesh->vertices[c].pos), point(mesh->vertices[a].pos));
                    const Point cross{ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]};
                    const auto area = dot(cross, point(frame.normal));
                    if (area < 0) throw std::invalid_argument("Cavity lighting refinement reverses winding");
                    if (area > 0) mesh->indices.insert(mesh->indices.end(), {a,b,c});
                };
                for (std::uint32_t i = 0; i < n; ++i) for (std::uint32_t j = 0; j < n-i; ++j) {
                    emit(row(i,j), row(i+1,j), row(i,j+1));
                    if (j+1 < n-i) emit(row(i+1,j), row(i+1,j+1), row(i,j+1));
                }
            }
            draw.indices.count = static_cast<std::uint32_t>(mesh->indices.size())-draw.indices.first;
            draw.needs_lighting = false;
        }
    }
    if (std::find(handled.begin(), handled.end(), false) != handled.end()) throw std::invalid_argument("Missing cavity lighting model");
    result.mesh = std::move(mesh);
    return result;
}
} // namespace goldcraft::lighting
