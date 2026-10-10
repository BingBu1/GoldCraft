#pragma once
#include "goldcraft/map_edits.hpp"
#include <cmath>
#include <limits>

namespace goldcraft::mining {
// Grid coordinates in the target's GoldSrc frame, one Minecraft block per cell.
// Keep the sampled cell identity separate from the backed-off trace endpoint.
struct Cell {
    std::int32_t x = 0, y = 0, z = 0;
    bool operator==(const Cell &) const = default;
};
inline bool valid_cell(Cell cell) noexcept {
    return cell.x >= -1024 && cell.x < 1024 && cell.y >= -1024 && cell.y < 1024 &&
           cell.z >= -1024 && cell.z < 1024;
}
inline edits::Box cell_box(Cell cell) {
    if (!valid_cell(cell)) throw ProtocolError("Mining cell bounds");
    edits::Box box;
    const std::int32_t axes[]{cell.x, cell.y, cell.z};
    for (int i = 0; i < 3; ++i) {
        box.min[i] = static_cast<float>(axes[i]) * units_per_block;
        box.max[i] = box.min[i] + units_per_block;
    }
    return box;
}
inline Cell surface_cell(Vec3 from, Vec3 to, Vec3 normal, float plane_distance) {
    const double a[]{from.x, from.y, from.z}, b[]{to.x, to.y, to.z};
    const double n[]{normal.x, normal.y, normal.z};
    double distance = plane_distance, speed = 0, length = 0;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || !std::isfinite(n[i]))
            throw ProtocolError("Nonfinite mining surface");
        distance -= a[i] * n[i];
        speed += (b[i] - a[i]) * n[i];
        length += n[i] * n[i];
    }
    if (!std::isfinite(distance) || length < 0.98 || length > 1.02 || speed >= -1e-6)
        throw ProtocolError("Invalid mining surface plane");
    const double fraction = distance / speed;
    if (fraction < 0 || fraction > 1.000001)
        throw ProtocolError("Mining plane outside validated ray");
    std::int32_t cell[3];
    for (int i = 0; i < 3; ++i) {
        // Native point traces stop DIST_EPSILON before the plane. Intersect
        // the actual plane, then resolve a grid-boundary tie toward its solid
        // side. A fixed inward offset would skip thin material or grid edges.
        const double point = a[i] + (b[i] - a[i]) * fraction;
        if (!std::isfinite(point) || point < -32768 || point >= 32768)
            throw ProtocolError("Mining surface outside cell grid");
        double grid = point / units_per_block;
        const double boundary = std::round(grid);
        const double rounding = 32 * std::numeric_limits<double>::epsilon() *
            (1 + std::abs(a[i]) + std::abs(b[i]) + std::abs(plane_distance)) / units_per_block;
        if (std::abs(grid - boundary) <= rounding) grid = boundary;
        double index = std::floor(grid);
        if (n[i] > 0 && index == grid) --index;
        cell[i] = static_cast<std::int32_t>(index);
    }
    const Cell selected{cell[0], cell[1], cell[2]};
    if (!valid_cell(selected)) throw ProtocolError("Mining solid side outside grid");
    return selected;
}
} // namespace goldcraft::mining
