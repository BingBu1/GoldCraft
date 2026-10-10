#pragma once
#include "goldcraft/cavity_mesh.hpp"

namespace goldcraft::lighting {
// RGB coefficients in the same pre-gamma [0,1] units as GoldSrc luxels.
// Styles are evaluated by the existing SceneUBO, never baked at one instant.
struct Value {
    float rgb[3]{};
    std::uint32_t style = 0;
    bool operator==(const Value &) const = default;
};
static_assert(sizeof(Value) == 16 && sizeof(surface::Range) == 8);
struct Probe {
    carving::Point position;
    surface::Range values;
};
struct Model {
    std::uint32_t first_face = 0, face_count = 0;
    carving::HullView hull;
    std::span<const carving::Box> cuts;
    std::span<const Probe> probes;
    std::span<const Value> values;
};
struct Settings {
    double radius = 128, spacing = 16;
    std::size_t neighbours = 16;
    std::size_t probes = 1'048'576, values = 4'194'304;
    surface::Limits mesh;
};
struct Prepared {
    std::shared_ptr<const surface::Mesh> mesh;
    std::vector<cavity::Draw> draws;
    // Indexed exactly like the vertex buffer; original vertices have count=0.
    std::vector<surface::Range> vertices;
    std::vector<Value> values;
    std::size_t samples = 0, rays = 0, occluded = 0, operations = 0;
};
// Extend the original map's baked irradiance field into new surfaces. Gather
// nearby front-hemisphere probes through the edited point BSP, keeping blocked
// weights in the denominator (occlusion darkens instead of renormalizing to
// full brightness). This is an approximate irradiance extension, not a new RAD
// compile or newly traced direct light. Missing probes reject; genuine black
// samples stay black. Sparse coefficients retain ALL contributing styles.
// Refine new triangles at preparation time; geometry/lighting/indices commit
// together. No original surface/lightmap or live resource is mutated.
Prepared prepare(const cavity::Prepared &geometry, std::span<const Model> models,
                 Settings settings = {});
// Shared bounded visibility primitive, also used to reject probe offsets that
// cross thin original solids. Endpoints alone never count as a solid interval.
// Hull and cut data must be validated once before querying (prepare does so).
bool visible(carving::HullView hull, std::span<const carving::Box> cuts,
             carving::Point start, carving::Point end, std::size_t &operations,
             std::size_t limit);
} // namespace goldcraft::lighting
