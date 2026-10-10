#pragma once
#include "goldcraft/surface_mesh.hpp"

namespace goldcraft::cavity {
struct Model {
    std::uint32_t first_face = 0, face_count = 0;
    carving::HullView hull;
    std::span<const carving::Box> boxes;
};
struct Draw {
    surface::Range indices, instances;
    std::uint32_t source_face = 0, first_face = 0, face_count = 0;
    carving::Box bounds{};
    // These are newly exposed surfaces, not samples of the donor's lightmap.
    // A live renderer must prepare their lighting before accepting the edit.
    bool needs_lighting = true;
};
struct Limits {
    surface::Limits mesh;
    std::size_t instances = 262'144, draws = 65'536;
};
struct Prepared {
    std::shared_ptr<const surface::Mesh> mesh;
    std::vector<Draw> draws;
    std::size_t changed_faces = 0, walls = 0, fragments = 0, operations = 0;
};
// Prepare remaining faces AND cavity walls in one immutable candidate. Each
// model is supplied once so touching/overlapping cuts share one union boundary.
// The nearest eligible original triangle in that model supplies the material;
// face/triangle order breaks distance ties. Rotate its affine UV mapping onto
// the wall without changing texture scale or mirroring. Never borrow sky/water
// materials, another model's faces, or the donor's baked lighting. Original face
// ranges are preserved; walls have explicit separately grouped draw ranges.
// Invalid input, missing material and all work/capacity failures are atomic.
Prepared prepare(std::shared_ptr<const surface::Mesh> original, std::span<const Model> models,
                 Limits limits = {});
} // namespace goldcraft::cavity
