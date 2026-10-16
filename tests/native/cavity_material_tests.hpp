#pragma once
#include <bit>
#include <random>

// Differential oracle retains the original exhaustive donor search. The
// implementation under test is the private production material index.
namespace cavity_material_tests {
using namespace goldcraft;
using cavity::Limits;
using cavity::Model;
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

#include "../../native/common/cavity_material.inc"

Material exhaustive(const surface::Mesh &mesh, const Model &model, const Point &center, Work &work) {
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
            // BSP polygons may wind opposite to their outward surface normal.
            // Use the surface orientation for UV transport and retain the
            // donor's winding for the host renderer's culling convention.
            const auto &frame = mesh.frames[mesh.indices[at]];
            const Point outward{frame.normal[0], frame.normal[1], frame.normal[2]};
            const auto orientation = dot(n, outward);
            if (orientation == 0) continue;
            result.face = static_cast<std::uint32_t>(face);
            result.closest = q;
            result.reversed = orientation < 0;
            result.normal = scale(n, (result.reversed ? -1 : 1) / std::sqrt(squared));
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

void equal(const Material &a, const Material &b) {
    assert(a.face == b.face && a.closest == b.closest && a.normal == b.normal && a.reversed == b.reversed);
    for (int i = 0; i < 2; ++i) assert(a.gradient[i] == b.gradient[i] && a.uv[i] == b.uv[i]);
}
void run() {
    surface::Mesh mesh;
    std::mt19937 random(0x16cafe);
    auto coordinate = [&] { return (static_cast<int>(random() % 4001) - 2000) * .25f; };
    for (unsigned f = 0; f < 2048; ++f) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const float x = coordinate(), y = coordinate(), z = coordinate();
        const float extent = f % 11 ? 16.f : .0001f;
        for (const auto xy : {std::array<float,2>{0,0}, {extent,0}, {extent,extent}}) {
            surface::Vertex v{};
            v.pos[0]=x+xy[0]; v.pos[1]=y+xy[1]; v.pos[2]=z;
            v.texcoord[0]=xy[0]*.25f + f; v.texcoord[1]=xy[1]*-.5f;
            if (f % 13 == 0) v.texcoord[1]=0;
            mesh.vertices.push_back(v);
            surface::Frame frame{};
            frame.normal[2]=f % 17 == 0 ? 0.f : 1.f;
            mesh.frames.push_back(frame);
        }
        // Include exact duplicate geometry with different material/UV data.
        if (f % 7 == 1) for (unsigned j=0;j<3;++j)
            std::copy_n(mesh.vertices[base-3+j].pos,3,mesh.vertices[base+j].pos);
        const auto at=static_cast<std::uint32_t>(mesh.indices.size());
        mesh.indices.insert(mesh.indices.end(),{base,base+1,base+2});
        if (f % 3 == 0) std::swap(mesh.indices[at+1],mesh.indices[at+2]);
        mesh.instances.push_back({});
        mesh.faces.push_back({{at,3},{},{f,f%19 ? 1u : 0u},1,f%23 != 0});
    }
    // Cancellation and a very thin triangle near the supported coordinate limit.
    for (unsigned i=0;i<3;++i) {
        auto &p=mesh.vertices[3*100+i].pos;
        p[0]=999999.f+(i==1 ? .0625f : 0.f);
        p[1]=i==2 ? 1e-20f : 0.f;
        p[2]=-999999.f;
    }
    std::vector<Point> queries{{999999.,0.,-999999.}, {999999.03125,1e-20,-999998.999}, {0,0,0}};
    for (int i=0;i<500;++i) queries.push_back({coordinate(),coordinate(),coordinate()});
    for (unsigned f=0;f<mesh.faces.size();f+=7) {
        const auto at=mesh.faces[f].forward.first;
        queries.push_back(point(mesh.vertices[mesh.indices[at]]));
        const auto &a=mesh.vertices[mesh.indices[at]], &b=mesh.vertices[mesh.indices[at+1]],
                   &c=mesh.vertices[mesh.indices[at+2]];
        queries.push_back(scale(add(add(point(a),point(b)),point(c)),1./3));
    }
    std::size_t comparisons=0;
    for (const auto range : {std::array<std::uint32_t,2>{0,2048}, {100,80}}) {
        const Model model{range[0],range[1],{}, {}};
        Work indexed{Limits{}};
        const MaterialIndex index(mesh,model,true,indexed);
        for (const auto &q:queries) {
            Work reference{Limits{}};
            equal(index.find(q,indexed),exhaustive(mesh,model,q,reference));
            ++comparisons;
        }
        const auto expected=index.find(queries[0],indexed);
        auto limited=Limits{}; limited.mesh.operations=0;
        Work exhausted{limited};
        bool rejected=false;
        try { (void)index.find(queries[0],exhausted); }
        catch(const std::length_error &) { rejected=true; }
        assert(rejected);
        equal(index.find(queries[0],indexed),expected);
        rejected=false;
        try { const MaterialIndex failed(mesh,model,true,exhausted); }
        catch(const std::length_error &) { rejected=true; }
        assert(rejected);
    }
    std::cout << "Material BVH: " << comparisons << " exact exhaustive comparisons, ties, invalid donors, "
                 "model isolation, thin geometry and budget rejection passed\n";
}
} // namespace cavity_material_tests
