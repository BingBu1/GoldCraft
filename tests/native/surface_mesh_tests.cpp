#include "goldcraft/surface_mesh.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace goldcraft::surface;
namespace {
std::shared_ptr<Mesh> fixture() {
    auto mesh = std::make_shared<Mesh>();
    for (const auto xy : {std::array<float, 2>{0, 0}, {128, 0}, {128, 128}, {0, 128}}) {
        const auto x = xy[0], y = xy[1];
        mesh->vertices.push_back(
            {{x, y, 0}, {x * .25f + 3, y * .5f - 7}, {x / 2048 + .0625f, y / 2048 + .125f}});
        mesh->frames.push_back(
            {{0, 0, 1}, {1, x / 256, 0}, {y / 512, 1, 0}, {x / 256, y / 512, 1}});
    }
    mesh->instances.push_back({{60001, 513}, {3, 15, 200, 255}, .015625f});
    mesh->instances.push_back({{60002, 514}, {4, 16, 201, 254}, .03125f});
    mesh->indices = {0, 1, 2, 0, 2, 3, 2, 1, 0, 3, 2, 0, 0, 1, 2, 0, 2, 3};
    mesh->faces = {{{0, 6}, {6, 6}, {0, 1}, 1, true}, {{12, 6}, {}, {1, 1}, 1, false}};
    return mesh;
}
double area(const Mesh &mesh, Range r) {
    double result = 0;
    for (std::size_t i = r.first; i < std::size_t(r.first) + r.count; i += 3) {
        const auto &a = mesh.vertices[mesh.indices[i]], &b = mesh.vertices[mesh.indices[i + 1]],
                   &c = mesh.vertices[mesh.indices[i + 2]];
        const auto z = (b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) -
                       (b.pos[1] - a.pos[1]) * (c.pos[0] - a.pos[0]);
        result += z / 2;
    }
    return result;
}
void attributes(const Mesh &mesh) {
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto &v = mesh.vertices[i];
        const auto &f = mesh.frames[i];
        assert(std::abs(v.texcoord[0] - (v.pos[0] * .25f + 3)) < 1e-5);
        assert(std::abs(v.texcoord[1] - (v.pos[1] * .5f - 7)) < 1e-5);
        assert(std::abs(v.lightmaptexcoord[0] - (v.pos[0] / 2048 + .0625f)) < 1e-6);
        assert(std::abs(v.lightmaptexcoord[1] - (v.pos[1] / 2048 + .125f)) < 1e-6);
        assert(f.normal[0] == 0 && f.normal[1] == 0 && f.normal[2] == 1);
        assert(f.s_tangent[0] == 1 && f.s_tangent[2] == 0 &&
               std::abs(f.s_tangent[1] - v.pos[0] / 256) < 1e-6);
        assert(f.t_tangent[1] == 1 && f.t_tangent[2] == 0 &&
               std::abs(f.t_tangent[0] - v.pos[1] / 512) < 1e-6);
        assert(f.smoothnormal[2] == 1 && std::abs(f.smoothnormal[0] - v.pos[0] / 256) < 1e-6 &&
               std::abs(f.smoothnormal[1] - v.pos[1] / 512) < 1e-6);
    }
}
template <class F> void rejected(F run) {
    bool failed = false;
    try {
        run();
    } catch (const std::exception &) {
        failed = true;
    }
    assert(failed);
}
} // namespace
int main() {
    const auto original = fixture();
    const auto baseline = *original;
    const Selection cut{0, 2, {{32, 32, -8}, {64, 64, 8}}};
    assert(prepare(original, {}).mesh == original);
    const Selection miss{0, 2, {{256, 256, -8}, {288, 288, 8}}};
    assert(prepare(original, {&miss, 1}).mesh == original);
    const auto edited = prepare(original, {&cut, 1});
    assert(edited.changed_faces == 1 && edited.fragments > 0 && *original == baseline);
    assert(edited.mesh != original && edited.mesh->instances == original->instances);
    assert(area(*edited.mesh, edited.mesh->faces[0].forward) == 16384 - 1024);
    assert(area(*edited.mesh, edited.mesh->faces[0].reverse) == -16384 + 1024);
    assert(area(*edited.mesh, edited.mesh->faces[1].forward) == 16384);
    assert(edited.mesh->faces[1].instances == original->faces[1].instances);
    for (std::size_t i = 0; i < original->vertices.size(); ++i) {
        assert(edited.mesh->vertices[i] == original->vertices[i]);
        assert(edited.mesh->frames[i] == original->frames[i]);
    }
    attributes(*edited.mesh);
    const std::array selections{cut, Selection{0, 1, {{64, 32, -8}, {96, 64, 8}}}};
    const auto adjacent = prepare(original, selections);
    assert(area(*adjacent.mesh, adjacent.mesh->faces[0].forward) == 16384 - 2048);
    attributes(*adjacent.mesh);
    const std::array overlaps{cut, Selection{0, 1, {{48, 32, -8}, {80, 64, 8}}}};
    const auto joined = prepare(original, overlaps);
    assert(area(*joined.mesh, joined.mesh->faces[0].forward) == 16384 - 1536);
    const std::array duplicates{cut, cut};
    const auto duplicate = prepare(original, duplicates);
    assert(area(*duplicate.mesh, duplicate.mesh->faces[0].forward) == 16384 - 1024);
    const Selection wipe{0, 1, {{-1, -1, -8}, {129, 129, 8}}};
    const auto removed = prepare(original, {&wipe, 1});
    assert(removed.mesh->faces[0].forward.count == 0 && removed.mesh->faces[0].reverse.count == 0);
    assert(removed.mesh->faces[1].forward.count == 6);
    auto live = edited.mesh;
    rejected([&] {
        auto l = Limits{};
        l.vertices = original->vertices.size();
        live = prepare(original, {&cut, 1}, l).mesh;
    });
    rejected([&] {
        auto l = Limits{};
        l.indices = original->indices.size();
        live = prepare(original, {&cut, 1}, l).mesh;
    });
    rejected([&] {
        auto l = Limits{};
        l.fragments = 1;
        live = prepare(original, {&cut, 1}, l).mesh;
    });
    rejected([&] {
        auto l = Limits{};
        l.operations = 1;
        live = prepare(original, {&cut, 1}, l).mesh;
    });
    assert(live == edited.mesh && *original == baseline);
    rejected([&] {
        auto bad = cut;
        bad.face_count = 3;
        (void)prepare(original, {&bad, 1});
    });
    rejected([&] {
        auto bad = cut;
        bad.face_count = 0;
        (void)prepare(original, {&bad, 1});
    });
    rejected([&] {
        auto bad = cut;
        bad.box.min[0] = bad.box.max[0];
        (void)prepare(original, {&bad, 1});
    });
    rejected([&] {
        auto bad = fixture();
        bad->indices[0] = 99;
        (void)prepare(bad, {});
    });
    rejected([&] {
        auto bad = fixture();
        bad->faces[0].forward.count = 5;
        (void)prepare(bad, {});
    });
    rejected([&] {
        auto bad = fixture();
        bad->faces[0].instances.count = 99;
        (void)prepare(bad, {});
    });
    rejected([&] {
        auto bad = fixture();
        bad->frames[0].smoothnormal[0] = std::numeric_limits<float>::quiet_NaN();
        (void)prepare(bad, {});
    });
    rejected([&] {
        auto bad = fixture();
        bad->frames.pop_back();
        (void)prepare(bad, {});
    });
    assert(prepare(original, {}).mesh == original && *original == baseline);
    std::cout << "Surface mesh attributes, union cuts, ranges, reverse winding, "
                 "immutable restoration and rejection passed\n";
}
