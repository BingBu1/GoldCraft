#include "goldcraft/cavity_lighting.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace goldcraft;
namespace {
cavity::Prepared geometry() {
    auto mesh = std::make_shared<surface::Mesh>();
    mesh->vertices = {{{0,0,0},{0,0}}, {{0,16,0},{1,0}}, {{0,16,16},{1,1}}, {{0,0,16},{0,1}}};
    mesh->frames.resize(4, {{1,0,0},{0,1,0},{0,0,1},{1,0,0}});
    mesh->instances.push_back({});
    mesh->indices = {0,1,2,0,2,3};
    mesh->faces.resize(1);
    cavity::Draw draw{{0,6},{0,1},0,0,1,{{0,0,0},{0,16,16}},true};
    return {mesh,{draw}};
}
void close(double a, double b) { assert(std::abs(a-b) < 1e-6); }
template<class F> void rejected(F f) {
    bool failed = false;
    try { f(); } catch (const std::exception &) { failed = true; }
    assert(failed);
}
std::vector<lighting::Value> at_origin(const lighting::Prepared &baked) {
    for (const auto &draw : baked.draws) for (std::size_t i = draw.indices.first; i < std::size_t(draw.indices.first)+draw.indices.count; ++i) {
        const auto index = baked.mesh->indices[i];
        const auto &v = baked.mesh->vertices[index];
        if (v.pos[0] == 0 && v.pos[1] == 0 && v.pos[2] == 0) {
            const auto range = baked.vertices[index];
            return {baked.values.begin()+range.first,baked.values.begin()+range.first+range.count};
        }
    }
    assert(false); return {};
}
} // namespace
int main() {
    const auto source = geometry();
    const auto original = *source.mesh;
    const std::array values{lighting::Value{{.8f,.4f,.2f},0},lighting::Value{{.1f,.2f,.3f},200}};
    const std::array probes{lighting::Probe{{32,8,8},{0,2}}};
    const carving::HullView empty{{},{},-1};
    lighting::Model model{0,1,empty,{},probes,values};
    auto baked = lighting::prepare(source,{&model,1});
    assert(!baked.draws[0].needs_lighting && baked.samples > 4 && baked.rays > 4 && !baked.occluded);
    assert(baked.mesh->faces == source.mesh->faces && baked.mesh->instances == source.mesh->instances);
    assert(*source.mesh == original && source.draws[0].needs_lighting);
    for (std::size_t i = 0; i < source.mesh->vertices.size(); ++i) {
        assert(source.mesh->vertices[i] == baked.mesh->vertices[i]);
        assert(source.mesh->frames[i] == baked.mesh->frames[i]);
    }
    auto origin = at_origin(baked);
    assert(origin == std::vector(values.begin(),values.end()));
    // A thin slab blocks every coefficient; removing its full intersection
    // restores them. Adjacent cuts have no artificial lighting seam.
    const std::array<carving::Plane,2> planes{{{{1,0,0},16},{{1,0,0},16.000000001}}};
    const std::array<carving::HullNode,2> nodes{{{0,{1,-1}},{1,{-1,-2}}}};
    model.hull = {planes,nodes,0};
    auto dark = lighting::prepare(source,{&model,1});
    assert(dark.values.empty() && dark.occluded == dark.rays && dark.rays);
    const std::array<carving::Box,2> cuts{{{{15,-1,-1},{16,17,17}},{{16,-1,-1},{17,17,17}}}};
    model.cuts = cuts;
    const auto opened = lighting::prepare(source,{&model,1});
    assert(at_origin(opened) == origin && opened.occluded == 0);
    model.cuts = {};
    assert(lighting::prepare(source,{&model,1}).values.empty());
    // Occluded red must not leak, and visible green must not renormalize away
    // the missing hemisphere. Both probes have equal distance and cosine.
    const std::array<carving::Plane,2> corner_planes{{{{1,0,0},16},{{0,1,0},0}}};
    const std::array<carving::HullNode,2> corner_nodes{{{0,{1,-1}},{1,{-2,-1}}}};
    const std::array corner_values{lighting::Value{{1,0,0},7},lighting::Value{{0,1,0},8}};
    const std::array corner_probes{lighting::Probe{{32,8,0},{0,1}},lighting::Probe{{32,-8,0},{1,1}}};
    model = {0,1,{corner_planes,corner_nodes,0},{},corner_probes,corner_values};
    auto corner = at_origin(lighting::prepare(source,{&model,1}));
    assert(corner.size() == 1 && corner[0].style == 8);
    close(corner[0].rgb[0],0); close(corner[0].rgb[1],.5); close(corner[0].rgb[2],0);
    // More than four contributing styles remain independent (no truncation to
    // the donor face's original four slots). Nearest-probe selection is exact.
    std::vector<lighting::Value> many_values;
    std::vector<lighting::Probe> many_probes;
    for (std::uint32_t i = 0; i < 40; ++i) {
        many_values.push_back({{1,1,1},i}); many_probes.push_back({{double(16+i),0,0},{i,1}});
    }
    model = {0,1,empty,{},many_probes,many_values};
    lighting::Settings settings;
    settings.neighbours = 8;
    auto styles = at_origin(lighting::prepare(source,{&model,1},settings));
    assert(styles.size() == 8);
    double sum = 0;
    for (std::uint32_t i = 0; i < styles.size(); ++i) { assert(styles[i].style == i); sum += styles[i].rgb[0]; }
    close(sum,1);
    settings.radius = 8;
    assert(lighting::prepare(source,{&model,1},settings).values.empty());
    auto live = baked.mesh;
    for (int i = 0; i < 6; ++i) rejected([&] {
        lighting::Settings limited;
        if (i == 0) limited.mesh.operations = 1;
        if (i == 1) limited.mesh.vertices = original.vertices.size();
        if (i == 2) limited.mesh.indices = original.indices.size();
        if (i == 3) limited.values = 1;
        if (i == 4) limited.probes = 1;
        if (i == 5) limited.neighbours = 0;
        live = lighting::prepare(source,{&model,1},limited).mesh;
    });
    assert(live == baked.mesh && *source.mesh == original);
    rejected([&] { auto missing = model; missing.probes = {}; (void)lighting::prepare(source,{&missing,1}); });
    rejected([&] { auto missing = model; missing.first_face = 1; (void)lighting::prepare(source,{&missing,1}); });
    rejected([&] { auto invalid = model; invalid.hull.root = 2; (void)lighting::prepare(source,{&invalid,1}); });
    rejected([&] { many_values[0].style = 255; (void)lighting::prepare(source,{&model,1}); });
    many_values[0].style = 0;
    rejected([&] { many_values[0].rgb[0] = std::numeric_limits<float>::quiet_NaN(); (void)lighting::prepare(source,{&model,1}); });
    many_values[0].rgb[0] = 1;
    const auto repeated = lighting::prepare(source,{&model,1});
    const auto again = lighting::prepare(source,{&model,1});
    assert(*repeated.mesh == *again.mesh && repeated.values == again.values && repeated.vertices == again.vertices);
    std::cout << "Cavity irradiance: thin-solid occlusion, adjacent openings, all styles, exact nearest probes, "
                 "refinement, deterministic restoration and bounded atomic failure passed\n";
}
