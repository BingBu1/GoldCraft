#pragma once
#include "goldcraft/world_carving.hpp"

namespace goldcraft::carving {
struct Face {
    Plane plane; // Outward normal; the convex cell is on its non-positive side.
    std::vector<Point> vertices;
};
struct Cell {
    std::vector<Face> faces;
};
struct Volume {
    Box region;
    std::vector<Cell> cells;
    std::size_t operations = 0;
};
// Solid point-BSP volume inside a finite local region, minus the UNION of cuts.
// No part of the outside region is implied. Material is retained as disjoint
// convex cells, including the new cavity walls. Empty/water leaves stay empty.
Volume carve_volume(HullView hull, Box region, std::span<const Box> cuts, Limits limits = {4096, 4'194'304});

struct CollisionCell {
    Box bounds;
    std::vector<Plane> planes;
};
struct CollisionVolume {
    std::vector<CollisionCell> cells;
    std::size_t operations = 0;
};
// Convex-cell Minkowski sum with the reflected axis-aligned body. Original
// face planes alone are insufficient: axes and edge/axis bevels are included.
// Supply the volume produced by carve_volume, with its closed convex cells intact.
// Point bodies are allowed. This does not recover clip-only map brushes and
// must not replace an unedited native hull with the point BSP's geometry.
CollisionVolume expand_volume(const Volume &volume, Box body, Limits limits = {4096, 4'194'304});
bool contains(const CollisionVolume &volume, Point point);
struct VolumeTrace {
    double fraction = 1;
    Plane plane{};
    bool start_solid = false, all_solid = false;
};
struct CollisionSpan {
    double begin, end;
    Plane enter{}, leave{};
};
// Sorted, disjoint interior intervals on the line start+t*(end-start), clipped
// only by their overlap with [0,1]. Unbounded endpoints are retained for CSG.
std::vector<CollisionSpan> volume_spans(const CollisionVolume &volume, Point start, Point end);
std::vector<CollisionSpan> merge_spans(std::vector<CollisionSpan> spans);
VolumeTrace trace_spans(std::span<const CollisionSpan> spans, Point start, Point end,
                       bool start_solid, bool end_solid, double margin);
// Sweep the body center through the expanded cell union. Boundary ties use a
// consistent global plane orientation, so shared partition faces cannot form
// zero-width tunnels. Margin is a contact backoff distance, not hole expansion.
// all_solid includes both endpoints; fraction remains 1 for all-solid traces.
VolumeTrace trace_volume(const CollisionVolume &volume, Point start, Point end, double margin = 0.03125);
} // namespace goldcraft::carving
