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
void cached_revisions() {
    const auto source=geometry();
    const std::array<carving::Plane,2> planes{{{{1,0,0},16},{{1,0,0},17}}};
    const std::array<carving::HullNode,2> nodes{{{0,{1,-1}},{1,{-1,-2}}}};
    const std::array values{lighting::Value{{.8f,.4f,.2f},0},lighting::Value{{.1f,.2f,.3f},200}};
    std::vector<lighting::Probe> probes{{{32,8,8},{0,2}}};
    std::vector<carving::Box> cuts{{{1024,1024,1024},{1056,1056,1056}}};
    lighting::Settings settings;settings.cache_bytes=1024*1024;
    lighting::Model model{0,1,{planes,nodes,0},cuts,probes,values};
    auto before=lighting::prepare(source,{&model,1},settings);
    assert(before.caches.size()==1 && before.values.empty() && before.occluded);
    const auto compare=[&](bool reusable) {
        model.previous=before.caches[0].samples;
        const auto actual=lighting::prepare(source,{&model,1},settings);
        auto coldSettings=settings;coldSettings.cache_bytes=0;
        const auto expected=lighting::prepare(source,{&model,1},coldSettings);
        assert(*actual.mesh==*expected.mesh && actual.vertices==expected.vertices && actual.values==expected.values);
        assert(actual.samples==expected.samples && actual.rays==expected.rays && actual.occluded==expected.occluded);
        assert((actual.reused_samples>0)==reusable);
        assert(actual.caches.size()==1 && lighting::cache_bytes(*actual.caches[0].samples)<=settings.cache_bytes);
        return actual;
    };
    auto same=compare(true);assert(same.reused_samples==same.samples);
    // Cached allocation hints must respect a tighter caller budget without
    // rejecting geometry that exactly fits, or changing any generated data.
    auto tight=settings;
    tight.mesh.vertices=same.mesh->vertices.size();tight.mesh.indices=same.mesh->indices.size();
    tight.values=std::max(same.values.size(),values.size());
    model.previous=before.caches[0].samples;
    const auto exact=lighting::prepare(source,{&model,1},tight);
    assert(*exact.mesh==*same.mesh && exact.values==same.values && exact.vertices==same.vertices);
    auto starved=settings;starved.mesh.operations=32;
    rejected([&]{lighting::prepare(source,{&model,1},starved);});
    assert(lighting::cache_bytes(*before.caches[0].samples)<=settings.cache_bytes);
    cuts.insert(cuts.begin(),carving::Box{{-2048,-2048,-2048},{-2016,-2016,-2016}});model.cuts=cuts;
    before=compare(true); // Added cut sorts before the original; no prefix assumption.
    cuts.push_back({{15,-1,-1},{18,17,17}});model.cuts=cuts;
    before=compare(false);assert(!before.values.empty()); // Nearby opening changes black coefficients.
    probes.insert(probes.begin(),lighting::Probe{{8192,8192,8192},{0,2}});model.probes=probes;
    before=compare(true); // Far probe insertion shifts indices, preserves nearest/tie order.
    probes.insert(probes.begin(),lighting::Probe{{24,4,4},{1,1}});model.probes=probes;
    before=compare(false); // New nearby probe changes styles/weights.
    std::swap(probes[0],probes[2]);model.probes=probes;
    before=compare(false); // Reordered probes cannot reuse distance-tie decisions.
    cuts.pop_back();model.cuts=cuts;
    before=compare(false);assert(before.values.empty()); // Restore solid geometry.
    auto changedValues=values;changedValues[0].rgb[0]=.3f;model.values=changedValues;
    before=compare(false);
    auto changedPlanes=planes;changedPlanes[0].distance=15;model.hull.planes=changedPlanes;
    before=compare(false);
    settings.radius=64;before=compare(false);
    settings.neighbours=1;before=compare(false);
    model.previous=before.caches[0].samples;
    settings.cache_bytes=1;
    const auto bounded=lighting::prepare(source,{&model,1},settings);
    assert(bounded.caches.empty());
    const std::atomic_bool cancelled{true};
    rejected([&]{lighting::prepare(source,{&model,1},settings,&cancelled);});
}
} // namespace
int main() {
    cached_revisions();
    const auto source = geometry();
    const auto original = *source.mesh;
    const std::array values{lighting::Value{{.8f,.4f,.2f},0},lighting::Value{{.1f,.2f,.3f},200}};
    const std::array probes{lighting::Probe{{32,8,8},{0,2}}};
    const carving::HullView empty{{},{},-1};
    lighting::Model model{0,1,empty,{},probes,values};
    const std::atomic_bool cancelled{true};
    rejected([&] { (void)lighting::prepare(source,{&model,1},{},&cancelled); });
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
    // Light-only compaction must retain sub-epsilon solid gaps. Duplicated
    // adjacent cuts may coalesce, but may not change any sample coefficient.
    std::vector<carving::Box> duplicate(cuts.begin(), cuts.end());
    duplicate.insert(duplicate.end(), cuts.begin(), cuts.end());
    model.cuts = duplicate;
    const auto compacted = lighting::prepare(source,{&model,1});
    assert(compacted.values == opened.values && compacted.vertices == opened.vertices &&
           *compacted.mesh == *opened.mesh && compacted.rays == opened.rays && compacted.occluded == opened.occluded);
    duplicate.resize(2);
    duplicate[1].min[0] = 16.000000002;
    model.cuts = duplicate;
    const auto gap = lighting::prepare(source,{&model,1});
    assert(gap.values.empty() && gap.occluded == gap.rays);
    rejected([&] {
        auto limited = lighting::Settings{}; limited.mesh.operations = 8;
        (void)lighting::prepare(source,{&model,1},limited);
    });
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
