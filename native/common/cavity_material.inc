// Private material lookup for an already validated immutable surface mesh.
// Included inside cavity_mesh.cpp's anonymous namespace.
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
struct Material {
    std::uint32_t face = 0;
    Point closest{}, normal{}, gradient[2]{};
    double uv[2]{};
    bool reversed = false;
};
class MaterialIndex {
    struct FaceBounds {
        carving::Box box;
        std::uint32_t face;
    };
    struct Node {
        carving::Box box;
        std::size_t begin, end, left = 0, right = 0;
    };
    const surface::Mesh &mesh_;
    std::vector<FaceBounds> faces_;
    std::vector<Node> nodes_;

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
    void face(std::uint32_t index, const Point &center, Material &result,
              double &best, std::size_t &best_triangle, Work &work) const {
        const auto &source = mesh_.faces[index];
        for (std::size_t at = source.forward.first; at < std::size_t(source.forward.first) + source.forward.count; at += 3) {
            work.consume(64);
            const auto &a = mesh_.vertices[mesh_.indices[at]], &b = mesh_.vertices[mesh_.indices[at + 1]],
                       &c = mesh_.vertices[mesh_.indices[at + 2]];
            const carving::Triangle triangle{point(a), point(b), point(c)};
            const auto e1 = sub(triangle[1], triangle[0]), e2 = sub(triangle[2], triangle[0]);
            const auto n = cross(e1, e2);
            const auto squared = dot(n, n);
            if (squared == 0) continue;
            const auto q = nearest(triangle, center, n), delta = sub(q, center);
            const auto candidate_distance = dot(delta, delta);
            if (candidate_distance > best || (candidate_distance == best &&
                (index > result.face || (index == result.face && at >= best_triangle)))) continue;
            Point gradients[2];
            for (int i = 0; i < 2; ++i)
                gradients[i] = scale(add(scale(cross(e2, n), double(b.texcoord[i]) - a.texcoord[i]),
                                         scale(cross(n, e1), double(c.texcoord[i]) - a.texcoord[i])), 1 / squared);
            const auto uv_normal = cross(gradients[0], gradients[1]);
            if (dot(uv_normal, uv_normal) == 0) continue;
            const auto &frame = mesh_.frames[mesh_.indices[at]];
            const Point outward{frame.normal[0], frame.normal[1], frame.normal[2]};
            const auto orientation = dot(n, outward);
            if (orientation == 0) continue;
            result.face = index;
            result.closest = q;
            result.reversed = orientation < 0;
            result.normal = scale(n, (result.reversed ? -1 : 1) / std::sqrt(squared));
            for (int i = 0; i < 2; ++i) {
                result.gradient[i] = gradients[i];
                result.uv[i] = a.texcoord[i] + dot(gradients[i], sub(q, triangle[0]));
            }
            best = candidate_distance;
            best_triangle = at;
        }
    }
    void visit(std::size_t index, const Point &center, Material &result,
               double &best, std::size_t &best_triangle, Work &work) const {
        work.consume(16);
        const auto &node = nodes_[index];
        if (farther(distance(node.box, center), best)) return;
        if (node.end - node.begin <= 4) {
            for (std::size_t i = node.begin; i < node.end; ++i) {
                work.consume(16);
                if (!farther(distance(faces_[i].box, center), best))
                    face(faces_[i].face, center, result, best, best_triangle, work);
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
            faces_.push_back({box, static_cast<std::uint32_t>(i)});
        }
        if (!faces_.empty()) build(0, faces_.size(), work);
    }
    Material find(const Point &center, Work &work) const {
        Material result;
        double best = std::numeric_limits<double>::infinity();
        std::size_t best_triangle = 0;
        if (!nodes_.empty()) visit(0, center, result, best, best_triangle, work);
        if (!std::isfinite(best)) throw std::invalid_argument("No cavity material in selected model");
        return result;
    }
};
