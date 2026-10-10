#pragma once
#include "goldcraft/world_volume.hpp"

namespace goldcraft::carving {
struct HullTrace : VolumeTrace {
    bool in_open = false, in_water = false;
};
// Immutable prepared collision overlay. H is the original body hull, O is
// original point-solid expanded by the body, and R is the remaining expansion:
// new solid = H minus (O minus R). Thus unedited native empty space stays empty,
// and native obstacles outside original material (observable clip-only space)
// are retained. BSP files cannot identify clip brushes hidden inside material;
// overlaps in O follow this explicit material-removal rule, not guessed provenance.
class EditedHull {
  public:
    EditedHull(HullView point_hull, HullView body_hull, Box body, std::span<const Box> cuts,
               Limits limits = {4096, 4'194'304});
    bool affects(Point start, Point end) const;
    int contents(Point point) const;
    HullTrace trace(Point start, Point end, double margin = 0.03125) const;
    std::size_t operations() const { return operations_; }

  private:
    HullView hull() const { return {planes_, nodes_, root_}; }
    std::vector<Plane> planes_;
    std::vector<HullNode> nodes_;
    std::int32_t root_;
    BoundsIndex affected_;
    CollisionVolume original_, remaining_;
    BoundsIndex original_index_, remaining_index_;
    std::size_t operations_ = 0, query_limit_;
};
} // namespace goldcraft::carving
