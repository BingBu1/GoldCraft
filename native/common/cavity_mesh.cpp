#include "goldcraft/cavity_mesh.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace goldcraft::cavity {
namespace {
using carving::Point;
Point add(const Point &a, const Point &b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Point sub(const Point &a, const Point &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Point scale(const Point &a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double dot(const Point &a, const Point &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Point cross(const Point &a, const Point &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Point point(const surface::Vertex &v) { return {v.pos[0], v.pos[1], v.pos[2]}; }
struct Work {
    Limits limits;
    std::size_t operations = 0, fragments = 0;
    void consume(std::size_t n) {
        if (n > limits.mesh.operations - operations)
            throw std::length_error("Cavity operation budget");
        operations += n;
    }
};
Point nearest(const carving::Triangle &t, const Point &p, const Point &n) {
    const auto e1 = sub(t[1], t[0]), e2 = sub(t[2], t[0]);
    const auto offset = sub(p, t[0]);
    const auto b1 = dot(cross(offset, e2), n) / dot(n, n);
    const auto b2 = dot(cross(e1, offset), n) / dot(n, n);
    if (b1 >= 0 && b2 >= 0 && b1 + b2 <= 1)
        return add(t[0], add(scale(e1, b1), scale(e2, b2)));
    Point result{};
    double distance = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 3; ++i) {
        const auto edge = sub(t[(i + 1) % 3], t[i]);
        const auto length = dot(edge, edge);
        const auto along = length ? std::clamp(dot(sub(p, t[i]), edge) / length, 0.0, 1.0) : 0;
        const auto q = add(t[i], scale(edge, along));
        const auto delta = sub(q, p);
        if (dot(delta, delta) < distance) {
            distance = dot(delta, delta);
            result = q;
        }
    }
    return result;
}
// Shortest rigid rotation preserves both UV gradients, including their skew
// and handedness. Antiparallel normals use a deterministic perpendicular axis.
Point rotate(const Point &p, const Point &from, const Point &to) {
    const auto v = cross(from, to);
    const auto cosine = std::clamp(dot(from, to), -1.0, 1.0);
    if (cosine < -1 + 1e-12) {
        std::size_t axis = 0;
        for (std::size_t i = 1; i < 3; ++i)
            if (std::abs(from[i]) < std::abs(from[axis])) axis = i;
        Point seed{};
        seed[axis] = 1;
        auto perpendicular = cross(from, seed);
        perpendicular = scale(perpendicular, 1 / std::sqrt(dot(perpendicular, perpendicular)));
        return sub(scale(perpendicular, 2 * dot(perpendicular, p)), p);
    }
    return add(p, add(cross(v, p), scale(cross(v, cross(v, p)), 1 / (1 + cosine))));
}
struct Material {
    std::uint32_t face = 0;
    Point closest{}, normal{}, gradient[2]{};
    double uv[2]{};
};
Material material(const surface::Mesh &mesh, const Model &model, const Point &center, Work &work) {
    Material result;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t face = model.first_face; face < std::size_t(model.first_face) + model.face_count; ++face) {
        work.consume(1);
        const auto &source = mesh.faces[face];
        if (!source.editable || !source.instances.count) continue;
        for (std::size_t at = source.forward.first; at < std::size_t(source.forward.first) + source.forward.count; at += 3) {
            work.consume(64);
            const auto &a = mesh.vertices[mesh.indices[at]], &b = mesh.vertices[mesh.indices[at + 1]],
                       &c = mesh.vertices[mesh.indices[at + 2]];
            const carving::Triangle triangle{point(a), point(b), point(c)};
            const auto e1 = sub(triangle[1], triangle[0]), e2 = sub(triangle[2], triangle[0]);
            const auto n = cross(e1, e2);
            const auto squared = dot(n, n);
            if (squared == 0) continue;
            const auto q = nearest(triangle, center, n), delta = sub(q, center);
            const auto distance = dot(delta, delta);
            if (distance >= best) continue;
            Point gradients[2];
            for (int i = 0; i < 2; ++i)
                gradients[i] = scale(add(scale(cross(e2, n), double(b.texcoord[i]) - a.texcoord[i]),
                                         scale(cross(n, e1), double(c.texcoord[i]) - a.texcoord[i])), 1 / squared);
            const auto uv_normal = cross(gradients[0], gradients[1]);
            if (dot(uv_normal, uv_normal) == 0) continue;
            result.face = static_cast<std::uint32_t>(face);
            result.closest = q;
            result.normal = scale(n, 1 / std::sqrt(squared));
            for (int i = 0; i < 2; ++i) {
                result.gradient[i] = gradients[i];
                result.uv[i] = a.texcoord[i] + dot(gradients[i], sub(q, triangle[0]));
            }
            best = distance;
        }
    }
    if (!std::isfinite(best)) throw std::invalid_argument("No cavity material in selected model");
    return result;
}
template <std::size_t N> void store(float (&out)[N], const std::array<double, N> &values) {
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = static_cast<float>(values[i]);
        if (!std::isfinite(out[i])) throw std::invalid_argument("Cavity attribute overflow");
    }
}
struct WallMaterial {
    carving::Wall wall;
    Material material;
    Point center;
};
void append(surface::Mesh &mesh, const surface::Mesh &original, const Model &model,
            const WallMaterial &item, std::vector<Draw> &draws, Work &work) {
    const auto &wall = item.wall;
    const auto &donor = item.material;
    const auto vertex_count = wall.vertices.size(), index_count = (vertex_count - 2) * 3;
    work.consume(vertex_count + index_count);
    if (vertex_count > work.limits.mesh.vertices - mesh.vertices.size() ||
        index_count > work.limits.mesh.indices - mesh.indices.size())
        throw std::length_error("Cavity mesh capacity");
    if (draws.empty() || draws.back().source_face != donor.face || draws.back().first_face != model.first_face) {
        const auto instances = original.faces[donor.face].instances;
        if (draws.size() >= work.limits.draws || instances.count > work.limits.instances - mesh.instances.size())
            throw std::length_error("Cavity draw or instance capacity");
        work.consume(instances.count);
        draws.push_back({{static_cast<std::uint32_t>(mesh.indices.size()), 0},
                         {static_cast<std::uint32_t>(mesh.instances.size()), instances.count},
                         donor.face, model.first_face, model.face_count,
                         {wall.vertices[0], wall.vertices[0]}});
        for (std::size_t i = instances.first; i < std::size_t(instances.first) + instances.count; ++i) {
            auto instance = original.instances[i];
            instance.packed_matId[1] = 0;
            std::fill(std::begin(instance.styles), std::end(instance.styles), std::uint8_t{255});
            mesh.instances.push_back(instance);
        }
    }
    const auto start = static_cast<std::uint32_t>(mesh.vertices.size());
    const Point gradients[]{rotate(donor.gradient[0], donor.normal, wall.normal),
                            rotate(donor.gradient[1], donor.normal, wall.normal)};
    for (const auto &p : wall.vertices) {
        surface::Vertex vertex{};
        surface::Frame frame{};
        store(vertex.pos, p);
        store(vertex.texcoord, std::array{donor.uv[0] + dot(gradients[0], sub(p, item.center)),
                                       donor.uv[1] + dot(gradients[1], sub(p, item.center))});
        store(frame.normal, wall.normal);
        store(frame.smoothnormal, wall.normal);
        store(frame.s_tangent, scale(gradients[0], 1 / std::sqrt(dot(gradients[0], gradients[0]))));
        store(frame.t_tangent, scale(gradients[1], 1 / std::sqrt(dot(gradients[1], gradients[1]))));
        mesh.vertices.push_back(vertex);
        mesh.frames.push_back(frame);
        for (int axis = 0; axis < 3; ++axis) {
            draws.back().bounds.min[axis] = std::min(draws.back().bounds.min[axis], p[axis]);
            draws.back().bounds.max[axis] = std::max(draws.back().bounds.max[axis], p[axis]);
        }
    }
    for (std::size_t i = 1; i + 1 < vertex_count; ++i) {
        const auto orientation = dot(cross(sub(wall.vertices[i], wall.vertices[0]),
                                           sub(wall.vertices[i + 1], wall.vertices[0])), wall.normal);
        if (orientation == 0) continue; // BSP clipping can preserve collinear boundary vertices.
        const auto quantized = dot(cross(sub(point(mesh.vertices[start + i]), point(mesh.vertices[start])),
                                         sub(point(mesh.vertices[start + i + 1]), point(mesh.vertices[start]))), wall.normal);
        // Collinear points and sub-float slivers have no rasterized area. Do
        // not emit zero-area draws; a winding reversal still rejects the edit.
        if (quantized == 0) continue;
        if (orientation < 0 || quantized < 0)
            throw std::invalid_argument("Cavity triangle loses winding or area at GPU precision");
        mesh.indices.push_back(start);
        mesh.indices.push_back(start + static_cast<std::uint32_t>(i));
        mesh.indices.push_back(start + static_cast<std::uint32_t>(i + 1));
    }
    draws.back().indices.count = static_cast<std::uint32_t>(mesh.indices.size()) - draws.back().indices.first;
    if (!draws.back().indices.count) {
        mesh.instances.resize(draws.back().instances.first);
        draws.pop_back();
    }
}
} // namespace
Prepared prepare(std::shared_ptr<const surface::Mesh> original, std::span<const Model> models, Limits limits) {
    if (!original || limits.instances > std::numeric_limits<std::uint32_t>::max() ||
        limits.draws > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Cavity original or draw limits");
    if (original->instances.size() > limits.instances) throw std::length_error("Cavity instance budget");
    Work work{limits};
    work.consume(models.size());
    std::vector<surface::Selection> selections;
    for (std::size_t i = 0; i < models.size(); ++i) {
        const auto &model = models[i];
        if (!model.face_count || model.first_face > original->faces.size() ||
            model.face_count > original->faces.size() - model.first_face)
            throw std::invalid_argument("Cavity model face range");
        work.consume(i + model.boxes.size());
        for (std::size_t j = 0; j < i; ++j)
            if (model.first_face < std::size_t(models[j].first_face) + models[j].face_count &&
                models[j].first_face < std::size_t(model.first_face) + model.face_count)
                throw std::invalid_argument("Cavity models overlap; supply each cut union once");
        for (const auto &box : model.boxes)
            selections.push_back({model.first_face, model.face_count, box});
    }
    auto mesh_limits = limits.mesh;
    mesh_limits.operations -= work.operations;
    const auto surfaces = surface::prepare(original, selections, mesh_limits);
    work.consume(surfaces.operations);
    work.fragments = surfaces.fragments;
    Prepared result{surfaces.mesh, {}, surfaces.changed_faces, 0, work.fragments, work.operations};
    std::shared_ptr<surface::Mesh> next;
    for (const auto &model : models) {
        if (model.boxes.empty()) continue;
        auto interior = carving::interior_walls(model.hull, model.boxes,
            {limits.mesh.fragments - work.fragments, limits.mesh.operations - work.operations});
        work.consume(interior.operations);
        std::vector<WallMaterial> walls;
        for (auto &wall : interior.walls) {
            if (work.fragments >= limits.mesh.fragments) throw std::length_error("Cavity fragment budget");
            ++work.fragments;
            work.consume(wall.vertices.size());
            Point center{};
            for (const auto &p : wall.vertices) center = add(center, p);
            center = scale(center, 1.0 / wall.vertices.size());
            const auto donor = material(*original, model, center, work);
            walls.push_back({std::move(wall), donor, center});
        }
        // One draw/instance range per source material in a model, independent
        // of the order in which different wall orientations were generated.
        const auto rounds = std::max<std::size_t>(1, std::bit_width(walls.size()));
        if (walls.size() > (limits.mesh.operations - work.operations) / rounds)
            throw std::length_error("Cavity material grouping budget");
        work.consume(walls.size() * rounds);
        std::stable_sort(walls.begin(), walls.end(), [](const auto &a, const auto &b) {
            return a.material.face < b.material.face;
        });
        for (const auto &wall : walls) {
            if (!next) next = std::make_shared<surface::Mesh>(*surfaces.mesh);
            append(*next, *original, model, wall, result.draws, work);
            ++result.walls;
        }
    }
    if (next) result.mesh = std::move(next);
    result.fragments = work.fragments;
    result.operations = work.operations;
    return result;
}
} // namespace goldcraft::cavity
