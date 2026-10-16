#pragma once
#include "goldcraft/cavity_lighting.hpp"
#include <chrono>
#include <bit>
#include <cassert>
#include <iostream>

namespace blast_mesh_tests {
using namespace goldcraft;
// Digest fields individually, excluding padding and work counters. This permits
// exact comparisons against an executable linked to the previous implementation.
struct Digest {
    std::uint64_t value = 14695981039346656037ull;
    template<class T> void add(T v) {
        for (auto b : std::bit_cast<std::array<unsigned char, sizeof(T)>>(v))
            value = (value ^ b) * 1099511628211ull;
    }
    void mesh(const surface::Mesh& m) {
        add(m.vertices.size()); add(m.indices.size()); add(m.faces.size());
        for (const auto& v : m.vertices) {
            for (auto f : v.pos) add(f);
            for (auto f : v.texcoord) add(f);
            for (auto f : v.lightmaptexcoord) add(f);
        }
        for (const auto& f : m.frames) {
            for (auto x : f.normal) add(x);
            for (auto x : f.s_tangent) add(x);
            for (auto x : f.t_tangent) add(x);
            for (auto x : f.smoothnormal) add(x);
        }
        for (auto x : m.indices) add(x);
        for (const auto& f : m.faces) {
            add(f.forward.first); add(f.forward.count); add(f.reverse.first); add(f.reverse.count);
            add(f.instances.first); add(f.instances.count); add(f.polygons); add(f.editable);
        }
        for (const auto& i : m.instances) {
            for (auto x : i.packed_matId) add(x);
            for (auto x : i.styles) add(x);
            add(i.diffusescale);
        }
    }
};
inline void run(std::shared_ptr<const surface::Mesh> mesh, carving::HullView hull,
                const std::vector<carving::Box>& distributed) {
    std::vector<std::vector<carving::Box>> cases;
    cases.push_back({distributed.begin(), distributed.begin() + 64});
    cases.push_back(distributed);
    for(int layers:{2,4}) {
        std::vector<carving::Box> cuts;
        for(int layer=0;layer<layers;++layer) for(auto box:distributed) {
            box.min[0]+=layer*32;box.max[0]+=layer*32;cuts.push_back(box);
        }
        cases.push_back(std::move(cuts));
    }
    for (int radius : {2, 4, 5}) {
        std::vector<carving::Box> cuts;
        for (int x = -radius; x <= radius; ++x)
            for (int y = -radius; y <= radius; ++y)
                for (int z = -radius; z <= radius; ++z)
                    cuts.push_back({{96. + x*32, 2880. + y*32, 384. + z*32},
                                    {128. + x*32, 2912. + y*32, 416. + z*32}});
        cases.push_back(std::move(cuts));
    }
    // Geometry is the real BSP. Probe coefficients are a deterministic test
    // irradiance field, not a claim to reproduce the map's baked lightmap.
    const std::array values{lighting::Value{{.7f,.2f,.4f},0}};
    std::vector<lighting::Probe> probes;
    for (std::size_t i=0; i<mesh->vertices.size(); i+=3) {
        carving::Point p{};
        for (int a=0; a<3; ++a) p[a]=mesh->vertices[i].pos[a]+mesh->frames[i].normal[a];
        probes.push_back({p,{0,1}});
    }
    for (std::size_t i=0; i<cases.size(); ++i) {
        const auto& cuts=cases[i];
        cavity::Model model{0,static_cast<std::uint32_t>(mesh->faces.size()),hull,cuts};
        auto limits=cavity::Limits{};
        bool defaultMesh=true,defaultLight=true;
        cavity::Prepared geometry;
        const auto start=std::chrono::steady_clock::now();
        try { geometry=cavity::prepare(mesh,{&model,1}); }
        catch(const std::length_error& e) {
            defaultMesh=false;
            std::cout << "blast default geometry rejected: " << i << " " << e.what() << '\n';
            limits.mesh.operations=2'000'000'000;
            geometry=cavity::prepare(mesh,{&model,1},limits);
        }
        const auto geometryEnd=std::chrono::steady_clock::now();
        lighting::Model light{model.first_face,model.face_count,hull,cuts,probes,values};
        lighting::Prepared baked;
        try { baked=lighting::prepare(geometry,{&light,1}); }
        catch(const std::length_error& e) {
            defaultLight=false;
            std::cout << "blast default lighting rejected: " << i << " " << e.what() << '\n';
            auto settings=lighting::Settings{}; settings.mesh.operations=2'000'000'000;
            baked=lighting::prepare(geometry,{&light,1},settings);
        }
        Digest digest; digest.mesh(*geometry.mesh);digest.mesh(*baked.mesh);
        for (const auto& d:baked.draws) {
            digest.add(d.indices.first);digest.add(d.indices.count);digest.add(d.source_face);
            digest.add(d.instances.first);digest.add(d.instances.count);
            for(auto x:d.bounds.min)digest.add(x);
            for(auto x:d.bounds.max)digest.add(x);
        }
        for(const auto& r:baked.vertices){digest.add(r.first);digest.add(r.count);}
        for(const auto& v:baked.values){for(auto x:v.rgb)digest.add(x);digest.add(v.style);}
        const auto end=std::chrono::steady_clock::now();
        std::cout << "{\"blastCase\":" << i << ",\"cuts\":" << cuts.size()
                  << ",\"defaultMesh\":" << defaultMesh << ",\"defaultLight\":" << defaultLight
                  << ",\"meshWork\":" << geometry.operations << ",\"lightWork\":" << baked.operations
                  << ",\"meshMs\":" << std::chrono::duration<double,std::milli>(geometryEnd-start).count()
                  << ",\"lightMs\":" << std::chrono::duration<double,std::milli>(end-geometryEnd).count()
                  << ",\"digest\":" << digest.value << "}\n";
        assert(defaultMesh && defaultLight);
    }
}
}
