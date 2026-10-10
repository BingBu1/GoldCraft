#include "goldcraft/cavity_mesh.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace goldcraft;
namespace {
std::shared_ptr<surface::Mesh> fixture() {
    auto mesh = std::make_shared<surface::Mesh>();
    for (int model = 0; model < 2; ++model) {
        const auto base = static_cast<std::uint32_t>(mesh->vertices.size());
        for (const auto xy : {std::array<float, 2>{0, 0}, {128, 0}, {128, 128}, {0, 128}}) {
            const float x = xy[0] + model * 256, y = xy[1];
            mesh->vertices.push_back({{x, y, 0}, {x * .25f + y * .125f + 3, -y * .5f - 7}, {.3f, .6f}});
            mesh->frames.push_back({{0, 0, 1}, {1, 0, 0}, {0, -1, 0}, {0, 0, 1}});
        }
        mesh->instances.push_back({{static_cast<std::uint16_t>(60001 + model), 513}, {3, 15, 200, 255}, .015625f});
        const auto first = static_cast<std::uint32_t>(mesh->indices.size());
        mesh->indices.insert(mesh->indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        mesh->faces.push_back({{first, 6}, {}, {static_cast<std::uint32_t>(model), 1}, 1, true});
    }
    return mesh;
}
double dot(const carving::Point &a, const carving::Point &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
carving::Point sub(const carving::Point &a, const carving::Point &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
carving::Point cross(const carving::Point &a, const carving::Point &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
carving::Point point(const float (&p)[3]) { return {p[0], p[1], p[2]}; }
void close(double a, double b) { assert(std::abs(a - b) < .00001); }
double verify(const cavity::Prepared &prepared, const surface::Mesh &original) {
    double area = 0;
    const auto &mesh = *prepared.mesh;
    for (const auto &draw : prepared.draws) {
        assert(draw.needs_lighting && draw.indices.count > 0 && draw.indices.count % 3 == 0);
        assert(draw.source_face >= draw.first_face && draw.source_face - draw.first_face < draw.face_count);
        assert(draw.instances.count == 1);
        const auto &instance = mesh.instances[draw.instances.first];
        const auto &source = original.instances[original.faces[draw.source_face].instances.first];
        assert(instance.packed_matId[0] == source.packed_matId[0] && instance.packed_matId[1] == 0);
        assert(instance.diffusescale == source.diffusescale);
        for (const auto style : instance.styles) assert(style == 255);
        for (std::size_t at = draw.indices.first; at < std::size_t(draw.indices.first) + draw.indices.count; at += 3) {
            const auto ai = mesh.indices[at], bi = mesh.indices[at + 1], ci = mesh.indices[at + 2];
            const auto a = point(mesh.vertices[ai].pos), b = point(mesh.vertices[bi].pos), c = point(mesh.vertices[ci].pos);
            const auto geometric = cross(sub(b, a), sub(c, a));
            const auto normal = point(mesh.frames[ai].normal);
            assert(dot(geometric, normal) > 0);
            area += std::sqrt(dot(geometric, geometric)) / 2;
            for (const auto index : {ai, bi, ci}) {
                const auto &v = mesh.vertices[index];
                const auto &frame = mesh.frames[index];
                for (int axis = 0; axis < 3; ++axis)
                    assert(v.pos[axis] >= draw.bounds.min[axis] && v.pos[axis] <= draw.bounds.max[axis]);
                assert(v.lightmaptexcoord[0] == 0 && v.lightmaptexcoord[1] == 0);
                close(dot(point(frame.normal), point(frame.normal)), 1);
                close(dot(point(frame.s_tangent), point(frame.normal)), 0);
                close(dot(point(frame.t_tangent), point(frame.normal)), 0);
                close(dot(point(frame.s_tangent), point(frame.s_tangent)), 1);
                close(dot(point(frame.t_tangent), point(frame.t_tangent)), 1);
                for (int axis = 0; axis < 3; ++axis) assert(frame.smoothnormal[axis] == frame.normal[axis]);
            }
            // Recover the two UV gradients independently from the output triangle.
            const auto e1 = sub(b, a), e2 = sub(c, a);
            carving::Point gradients[2];
            for (int uv = 0; uv < 2; ++uv) {
                const auto d1 = double(mesh.vertices[bi].texcoord[uv]) - mesh.vertices[ai].texcoord[uv];
                const auto d2 = double(mesh.vertices[ci].texcoord[uv]) - mesh.vertices[ai].texcoord[uv];
                for (int axis = 0; axis < 3; ++axis)
                    gradients[uv][axis] = (cross(e2, geometric)[axis] * d1 + cross(geometric, e1)[axis] * d2) / dot(geometric, geometric);
            }
            close(dot(gradients[0], gradients[0]), .25 * .25 + .125 * .125);
            close(dot(gradients[1], gradients[1]), .5 * .5);
            close(dot(gradients[0], gradients[1]), -.125 * .5);
            assert(dot(cross(gradients[0], gradients[1]), normal) < 0); // Mirrored donor stays mirrored.
        }
    }
    return area;
}
template <class F> void rejected(F run) {
    bool failed = false;
    try { run(); } catch (const std::exception &) { failed = true; }
    assert(failed);
}
} // namespace
int main() {
    const auto original = fixture();
    const auto baseline = *original;
    const std::array<carving::Plane, 1> planes{{{{0, 0, 1}, 0}}};
    const std::array<carving::HullNode, 1> nodes{{{0, {-1, -2}}}};
    const carving::HullView hull{planes, nodes, 0};
    const std::array<carving::Box, 1> boxes{{{{32, 32, -8}, {64, 64, 8}}}};
    const cavity::Model model{0, 1, hull, boxes};
    const auto carved = cavity::prepare(original, {&model, 1});
    assert(carved.changed_faces == 1 && carved.walls == 5 && carved.draws.size() == 1);
    close(verify(carved, *original), 2048);
    assert(*original == baseline);
    for (std::size_t i = 0; i < original->vertices.size(); ++i) {
        assert(carved.mesh->vertices[i] == original->vertices[i]);
        assert(carved.mesh->frames[i] == original->frames[i]);
    }
    const std::array<carving::Box, 2> adjacent{{boxes[0], {{64, 32, -8}, {96, 64, 8}}}};
    const cavity::Model union_model{0, 1, hull, adjacent};
    const auto joined = cavity::prepare(original, {&union_model, 1});
    close(verify(joined, *original), 3584);
    const std::array duplicate{boxes[0], boxes[0]};
    const cavity::Model duplicate_model{0, 1, hull, duplicate};
    const auto duplicated = cavity::prepare(original, {&duplicate_model, 1});
    assert(*duplicated.mesh == *carved.mesh && duplicated.draws.size() == carved.draws.size());
    const std::array<carving::Box, 1> other_boxes{{{{288, 32, -8}, {320, 64, 8}}}};
    const std::array models{model, cavity::Model{1, 1, hull, other_boxes}};
    const auto separate = cavity::prepare(original, models);
    assert(separate.draws.size() == 2);
    close(verify(separate, *original), 4096);
    assert(separate.draws[0].source_face == 0 && separate.draws[1].source_face == 1);
    const cavity::Model buried{0, 1, {{}, {}, -2}, boxes};
    const auto all_solid = cavity::prepare(original, {&buried, 1});
    assert(all_solid.walls == 6); // Includes antiparallel texture transport.
    close(verify(all_solid, *original), 4096);
    const cavity::Model empty{0, 1, {{}, {}, -1}, boxes};
    assert(cavity::prepare(original, {&empty, 1}).draws.empty());
    const auto repeated = cavity::prepare(original, {&model, 1});
    assert(*repeated.mesh == *carved.mesh);
    assert(cavity::prepare(original, {}).mesh == original && *original == baseline);
    auto live = carved.mesh;
    for (int limit = 0; limit < 6; ++limit) rejected([&] {
        auto capacity = cavity::Limits{};
        if (limit == 0) capacity.mesh.vertices = original->vertices.size();
        if (limit == 1) capacity.mesh.indices = original->indices.size();
        if (limit == 2) capacity.mesh.fragments = 1;
        if (limit == 3) capacity.mesh.operations = 1;
        if (limit == 4) capacity.instances = original->instances.size();
        if (limit == 5) capacity.draws = 0;
        live = cavity::prepare(original, {&model, 1}, capacity).mesh;
    });
    assert(live == carved.mesh && *original == baseline);
    rejected([&] { const std::array overlapping{model, model}; (void)cavity::prepare(original, overlapping); });
    rejected([&] { auto invalid = model; invalid.face_count = 3; (void)cavity::prepare(original, {&invalid, 1}); });
    rejected([&] { auto protected_mesh = fixture(); protected_mesh->faces[0].editable = false; (void)cavity::prepare(protected_mesh, {&model, 1}); });
    rejected([&] { auto invalid = fixture(); invalid->faces[0].instances.count = 0; (void)cavity::prepare(invalid, {&model, 1}); });
    rejected([&] { auto invalid = fixture(); for (auto &v : invalid->vertices) v.texcoord[1] = 0; (void)cavity::prepare(invalid, {&model, 1}); });
    rejected([&] { auto invalid = model; invalid.hull.root = 99; (void)cavity::prepare(original, {&invalid, 1}); });
    std::cout << "Cavity union walls, affine texture scale/skew/mirroring, material isolation, "
                 "unbaked lighting, grouped draws, restoration and atomic rejection passed\n";
}
