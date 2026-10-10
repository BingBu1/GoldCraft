#include "goldcraft/surface_mesh.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace goldcraft::surface {
namespace {
struct Work {
    Limits limits;
    std::size_t operations = 0, fragments = 0;
    void consume(std::size_t n) {
        if (n > limits.operations - operations)
            throw std::length_error("Surface mesh operation budget");
        operations += n;
    }
};
void range(Range r, std::size_t size, bool triangles = false) {
    if (r.first > size || r.count > size - r.first || (triangles && r.count % 3))
        throw std::invalid_argument("Surface mesh range");
}
template <std::size_t N> void finite(const float (&values)[N], bool position = false) {
    for (const float value : values)
        if (!std::isfinite(value) || (position && std::abs(value) > 1'000'000))
            throw std::invalid_argument("Surface mesh nonfinite attribute or coordinate");
}
void validate(const Mesh &mesh, Work &work) {
    if (mesh.vertices.size() != mesh.frames.size() || mesh.vertices.size() > work.limits.vertices ||
        mesh.indices.size() > work.limits.indices ||
        mesh.faces.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.instances.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Surface mesh capacity or attribute count");
    work.consume(mesh.vertices.size() + mesh.instances.size() + mesh.indices.size() +
                 mesh.faces.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto &v = mesh.vertices[i];
        const auto &f = mesh.frames[i];
        finite(v.pos, true);
        finite(v.texcoord);
        finite(v.lightmaptexcoord);
        finite(f.normal);
        finite(f.s_tangent);
        finite(f.t_tangent);
        finite(f.smoothnormal);
    }
    for (const auto &instance : mesh.instances)
        if (!std::isfinite(instance.diffusescale))
            throw std::invalid_argument("Surface mesh instance scale");
    for (auto index : mesh.indices)
        if (index >= mesh.vertices.size())
            throw std::invalid_argument("Surface mesh vertex index");
    for (const auto &face : mesh.faces) {
        range(face.forward, mesh.indices.size(), true);
        range(face.reverse, mesh.indices.size(), true);
        range(face.instances, mesh.instances.size());
    }
}
carving::Point point(const Vertex &v) { return {v.pos[0], v.pos[1], v.pos[2]}; }
bool overlaps(const carving::Triangle &t, const carving::Box &b) {
    for (int a = 0; a < 3; ++a)
        if (std::max({t[0][a], t[1][a], t[2][a]}) < b.min[a] ||
            std::min({t[0][a], t[1][a], t[2][a]}) > b.max[a])
            return false;
    return true;
}
template <std::size_t N, class T>
void interpolate(float (&out)[N], const float (T::*field)[N], const std::vector<T> &source,
                 const std::array<std::uint32_t, 3> &indices,
                 const std::array<double, 3> &weights) {
    for (std::size_t a = 0; a < N; ++a) {
        double value = 0;
        for (std::size_t i = 0; i < 3; ++i)
            value += (source[indices[i]].*field)[a] * weights[i];
        out[a] = static_cast<float>(value);
    }
    finite(out);
}
std::uint32_t append_vertex(Mesh &out, const Mesh &source, const carving::Vertex &v,
                            const std::array<std::uint32_t, 3> &indices, Work &work) {
    for (std::size_t i = 0; i < 3; ++i)
        if (v.weights[i] == 1 && v.weights[(i + 1) % 3] == 0 && v.weights[(i + 2) % 3] == 0)
            return indices[i];
    if (out.vertices.size() >= work.limits.vertices)
        throw std::length_error("Surface mesh vertex budget");
    Vertex vertex{};
    Frame frame{};
    for (int a = 0; a < 3; ++a)
        vertex.pos[a] = static_cast<float>(v.position[a]);
    finite(vertex.pos, true);
    interpolate(vertex.texcoord, &Vertex::texcoord, source.vertices, indices, v.weights);
    interpolate(vertex.lightmaptexcoord, &Vertex::lightmaptexcoord, source.vertices, indices,
                v.weights);
    interpolate(frame.normal, &Frame::normal, source.frames, indices, v.weights);
    interpolate(frame.s_tangent, &Frame::s_tangent, source.frames, indices, v.weights);
    interpolate(frame.t_tangent, &Frame::t_tangent, source.frames, indices, v.weights);
    interpolate(frame.smoothnormal, &Frame::smoothnormal, source.frames, indices, v.weights);
    const auto index = static_cast<std::uint32_t>(out.vertices.size());
    out.vertices.push_back(vertex);
    out.frames.push_back(frame);
    return index;
}
void append_indices(Mesh &mesh, std::span<const std::uint32_t> indices, Work &work) {
    if (indices.size() > work.limits.indices - mesh.indices.size())
        throw std::length_error("Surface mesh index budget");
    mesh.indices.insert(mesh.indices.end(), indices.begin(), indices.end());
}
Range clip_range(Mesh &out, const Mesh &source, Range input, std::span<const carving::Box> boxes,
                 Work &work, bool &changed) {
    Range result{static_cast<std::uint32_t>(out.indices.size()), 0};
    for (std::size_t at = input.first; at < std::size_t(input.first) + input.count; at += 3) {
        const std::array<std::uint32_t, 3> indices{source.indices[at], source.indices[at + 1],
                                                   source.indices[at + 2]};
        carving::Triangle triangle{point(source.vertices[indices[0]]),
                                   point(source.vertices[indices[1]]),
                                   point(source.vertices[indices[2]])};
        std::vector<carving::Box> nearby;
        work.consume(boxes.size() + 1);
        for (const auto &box : boxes)
            if (overlaps(triangle, box))
                nearby.push_back(box);
        if (nearby.empty()) {
            append_indices(out, indices, work);
            continue;
        }
        auto remaining = carving::subtract(
            triangle, nearby,
            {work.limits.fragments - work.fragments, work.limits.operations - work.operations});
        work.consume(remaining.operations);
        if (!remaining.changed) {
            append_indices(out, indices, work);
            continue;
        }
        changed = true;
        for (const auto &polygon : remaining.pieces) {
            if (work.fragments >= work.limits.fragments)
                throw std::length_error("Surface mesh fragment budget");
            ++work.fragments;
            std::vector<std::uint32_t> vertices;
            vertices.reserve(polygon.size());
            for (const auto &v : polygon)
                vertices.push_back(append_vertex(out, source, v, indices, work));
            for (std::size_t i = 1; i + 1 < vertices.size(); ++i) {
                const std::array<std::uint32_t, 3> t{vertices[0], vertices[i], vertices[i + 1]};
                append_indices(out, t, work);
            }
        }
    }
    result.count = static_cast<std::uint32_t>(out.indices.size()) - result.first;
    return result;
}
} // namespace
Prepared prepare(std::shared_ptr<const Mesh> original, std::span<const Selection> cuts,
                 Limits limits) {
    if (!original)
        throw std::invalid_argument("Missing original surface mesh");
    if (limits.vertices > std::numeric_limits<std::uint32_t>::max() ||
        limits.indices > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Surface mesh exceeds draw index width");
    Work work{limits};
    validate(*original, work);
    work.consume(cuts.size());
    for (const auto &cut : cuts) {
        range({cut.first_face, cut.face_count}, original->faces.size());
        if (!cut.face_count)
            throw std::invalid_argument("Empty surface selection");
        for (int a = 0; a < 3; ++a)
            if (!std::isfinite(cut.box.min[a]) || !std::isfinite(cut.box.max[a]) ||
                std::abs(cut.box.min[a]) > 1'000'000 || std::abs(cut.box.max[a]) > 1'000'000 ||
                cut.box.min[a] >= cut.box.max[a])
                throw std::invalid_argument("Surface mesh cut box");
    }
    if (cuts.empty())
        return {original, 0, 0, work.operations};
    auto next = std::make_shared<Mesh>(*original);
    next->indices.clear();
    std::size_t changed_faces = 0;
    for (std::size_t i = 0; i < original->faces.size(); ++i) {
        const auto &face = original->faces[i];
        std::vector<carving::Box> boxes;
        work.consume(cuts.size() + 1);
        if (face.editable)
            for (const auto &cut : cuts)
                if (i >= cut.first_face && i - cut.first_face < cut.face_count)
                    boxes.push_back(cut.box);
        bool changed = false;
        auto &target = next->faces[i];
        target.forward = clip_range(*next, *original, face.forward, boxes, work, changed);
        target.reverse = clip_range(*next, *original, face.reverse, boxes, work, changed);
        if (changed) {
            ++changed_faces;
            target.polygons = (target.forward.count + target.reverse.count) / 3;
        }
    }
    return {changed_faces ? std::move(next) : original, changed_faces, work.fragments,
            work.operations};
}
} // namespace goldcraft::surface
