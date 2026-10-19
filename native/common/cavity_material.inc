// Private material lookup for an already validated immutable surface mesh.
// Included inside cavity_mesh.cpp's anonymous namespace.
struct TriangleGeometry {
    carving::Triangle vertices;
    Point e1, e2, normal;
    double squared = 0;
};
Point nearest(const TriangleGeometry &geometry, const Point &p) {
    const auto &t = geometry.vertices;
    const auto &e1 = geometry.e1, &e2 = geometry.e2, &n = geometry.normal;
    const auto offset = sub(p, t[0]);
    const auto b1 = dot(cross(offset, e2), n) / geometry.squared;
    const auto b2 = dot(cross(e1, offset), n) / geometry.squared;
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
struct Material {
    std::uint32_t face = 0;
    Point closest{}, normal{}, gradient[2]{};
    double uv[2]{};
    bool reversed = false;
};
class MaterialIndex {
    struct TriangleData {
        TriangleGeometry geometry;
        Material mapping;
        bool mapping_ready = false, valid = false;
    };
    struct FaceBounds {
        carving::Box box;
        std::uint32_t face;
        std::unique_ptr<TriangleData[]> triangles;
        bool cache_attempted = false;
    };
    struct Node {
        carving::Box box;
        std::size_t begin, end, left = 0, right = 0;
    };
    const surface::Mesh &mesh_;
    std::vector<FaceBounds> faces_;
    std::vector<Node> nodes_;
    // Per-model, per-prepare scratch; never retained by the resulting mesh.
    // Once full, remaining faces use the same uncached calculation.
    static constexpr std::size_t cache_budget = 4 * 1024 * 1024;
    std::size_t cached_bytes_ = 0;

    TriangleGeometry geometry(std::size_t at) const {
        TriangleGeometry result;
        for (std::size_t i = 0; i < 3; ++i)
            result.vertices[i] = point(mesh_.vertices[mesh_.indices[at + i]]);
        result.e1 = sub(result.vertices[1], result.vertices[0]);
        result.e2 = sub(result.vertices[2], result.vertices[0]);
        result.normal = cross(result.e1, result.e2);
        result.squared = dot(result.normal, result.normal);
        return result;
    }
    void mapping(TriangleData &data, std::size_t at) const {
        if (data.mapping_ready) return;
        data.mapping_ready = true;
        const auto &g = data.geometry;
        const auto &a = mesh_.vertices[mesh_.indices[at]], &b = mesh_.vertices[mesh_.indices[at + 1]],
                   &c = mesh_.vertices[mesh_.indices[at + 2]];
        auto &result = data.mapping;
        for (int i = 0; i < 2; ++i)
            result.gradient[i] = scale(add(scale(cross(g.e2, g.normal), double(b.texcoord[i]) - a.texcoord[i]),
                                           scale(cross(g.normal, g.e1), double(c.texcoord[i]) - a.texcoord[i])), 1 / g.squared);
        const auto uv_normal = cross(result.gradient[0], result.gradient[1]);
        if (dot(uv_normal, uv_normal) == 0) return;
        const auto &frame = mesh_.frames[mesh_.indices[at]];
        const Point outward{frame.normal[0], frame.normal[1], frame.normal[2]};
        const auto orientation = dot(g.normal, outward);
        if (orientation == 0) return;
        result.reversed = orientation < 0;
        result.normal = scale(g.normal, (result.reversed ? -1 : 1) / std::sqrt(g.squared));
        for (int i = 0; i < 2; ++i) result.uv[i] = a.texcoord[i];
        data.valid = true;
    }

    std::size_t build(std::size_t begin, std::size_t end, Work &work) {
        work.consume(end - begin);
        auto box = faces_[begin].box;
        for (std::size_t i = begin + 1; i < end; ++i)
            for (int axis = 0; axis < 3; ++axis) {
                box.min[axis] = std::min(box.min[axis], faces_[i].box.min[axis]);
                box.max[axis] = std::max(box.max[axis], faces_[i].box.max[axis]);
            }
        const auto index = nodes_.size();
        nodes_.push_back({box, begin, end});
        if (end - begin <= 4) return index;
        int axis = 0;
        for (int i = 1; i < 3; ++i)
            if (box.max[i] - box.min[i] > box.max[axis] - box.min[axis]) axis = i;
        const auto middle = begin + (end - begin) / 2;
        std::nth_element(faces_.begin() + begin, faces_.begin() + middle, faces_.begin() + end,
            [&](const auto &a, const auto &b) {
                work.consume(1);
                const auto ac = a.box.min[axis] + a.box.max[axis];
                const auto bc = b.box.min[axis] + b.box.max[axis];
                return ac != bc ? ac < bc : a.face < b.face;
            });
        const auto left = build(begin, middle, work), right = build(middle, end, work);
        nodes_[index].left = left;
        nodes_[index].right = right;
        return index;
    }
    static double distance(const carving::Box &box, const Point &p) {
        double result = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const auto delta = std::max({box.min[axis] - p[axis], p[axis] - box.max[axis], 0.0});
            result += delta * delta;
        }
        return result;
    }
    static bool farther(double lower, double best) {
        // Bounds are broad-phase only. Round-off near a tie must cause extra
        // exact triangle checks, never change the winning material.
        return lower > best + std::abs(best) * (32 * std::numeric_limits<double>::epsilon());
    }
    void face(FaceBounds &entry, const Point &center, Material &result,
              double &best, std::size_t &best_triangle, Work &work) {
        const auto index = entry.face;
        const auto &source = mesh_.faces[index];
        bool initialize = false;
        if (!entry.cache_attempted) {
            entry.cache_attempted = true;
            const auto count = source.forward.count / 3;
            if (count <= (cache_budget - cached_bytes_) / sizeof(TriangleData)) {
                entry.triangles = std::make_unique<TriangleData[]>(count);
                cached_bytes_ += count * sizeof(TriangleData);
                initialize = true;
            }
        }
        for (std::size_t at = source.forward.first; at < std::size_t(source.forward.first) + source.forward.count; at += 3) {
            work.consume(64);
            TriangleData uncached;
            auto &data = entry.triangles ? entry.triangles[(at - source.forward.first) / 3] : uncached;
            if (initialize || !entry.triangles) data.geometry = geometry(at);
            if (data.geometry.squared == 0) continue;
            const auto q = nearest(data.geometry, center), delta = sub(q, center);
            const auto candidate_distance = dot(delta, delta);
            if (candidate_distance > best || (candidate_distance == best &&
                (index > result.face || (index == result.face && at >= best_triangle)))) continue;
            mapping(data, at);
            if (!data.valid) continue;
            result = data.mapping;
            result.face = index;
            result.closest = q;
            for (int i = 0; i < 2; ++i)
                result.uv[i] += dot(result.gradient[i], sub(q, data.geometry.vertices[0]));
            best = candidate_distance;
            best_triangle = at;
        }
    }
    void visit(std::size_t index, const Point &center, Material &result,
               double &best, std::size_t &best_triangle, Work &work) {
        work.consume(16);
        const auto &node = nodes_[index];
        if (farther(distance(node.box, center), best)) return;
        if (node.end - node.begin <= 4) {
            for (std::size_t i = node.begin; i < node.end; ++i) {
                work.consume(16);
                if (!farther(distance(faces_[i].box, center), best))
                    face(faces_[i], center, result, best, best_triangle, work);
            }
            return;
        }
        work.consume(32);
        auto first = node.left, second = node.right;
        if (distance(nodes_[second].box, center) < distance(nodes_[first].box, center))
            std::swap(first, second);
        visit(first, center, result, best, best_triangle, work);
        visit(second, center, result, best, best_triangle, work);
    }
public:
    MaterialIndex(const surface::Mesh &mesh, const Model &model, bool needed, Work &work) : mesh_(mesh) {
        if (!needed) return;
        for (std::size_t i = model.first_face; i < std::size_t(model.first_face) + model.face_count; ++i) {
            work.consume(1);
            const auto &source = mesh.faces[i];
            if (!source.editable || !source.instances.count || !source.forward.count) continue;
            const auto p = point(mesh.vertices[mesh.indices[source.forward.first]]);
            carving::Box box{p, p};
            work.consume(source.forward.count);
            for (std::size_t at = source.forward.first; at < std::size_t(source.forward.first) + source.forward.count; ++at) {
                const auto vertex = point(mesh.vertices[mesh.indices[at]]);
                for (int axis = 0; axis < 3; ++axis) {
                    box.min[axis] = std::min(box.min[axis], vertex[axis]);
                    box.max[axis] = std::max(box.max[axis], vertex[axis]);
                }
            }
            for (int axis = 0; axis < 3; ++axis) {
                // nearest() uses clamped edge parameters or nonnegative
                // barycentrics summing to at most one. Pad the bounds for
                // their double arithmetic, including cancellation at zero.
                const auto magnitude = std::max({1.0, std::abs(box.min[axis]), std::abs(box.max[axis])});
                const auto pad = magnitude * (128 * std::numeric_limits<double>::epsilon());
                box.min[axis] -= pad;
                box.max[axis] += pad;
            }
            faces_.push_back({box, static_cast<std::uint32_t>(i), {}, false});
        }
        if (!faces_.empty()) build(0, faces_.size(), work);
    }
    Material find(const Point &center, Work &work) {
        Material result;
        double best = std::numeric_limits<double>::infinity();
        std::size_t best_triangle = 0;
        if (!nodes_.empty()) visit(0, center, result, best, best_triangle, work);
        if (!std::isfinite(best)) throw std::invalid_argument("No cavity material in selected model");
        return result;
    }
};
