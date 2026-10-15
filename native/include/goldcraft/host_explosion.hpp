#pragma once
#include "goldcraft/map_mining.hpp"
#include <tuple>

namespace goldcraft::explosion {
constexpr std::size_t max_cells = 4096;
// Only the authenticated MC dedicated server submits the final vanilla-ray
// block list. Cells use the GoldSrc world grid, never client coordinates.
struct Event {
    std::uint64_t epoch = 0, policy = 0, event = 0;
    Vec3 center{}; // Minecraft coordinates
    float power = 0;
    std::vector<mining::Cell> cells;
};
inline void validate(const Event& e) {
    if (!e.epoch || !e.policy || !e.event || !std::isfinite(e.power) || e.power <= 0 || e.power > 16 ||
        !std::isfinite(e.center.x) || !std::isfinite(e.center.y) || !std::isfinite(e.center.z) ||
        std::abs(e.center.x) > 1024 || std::abs(e.center.z) > 1024 || std::abs(e.center.y - minecraft_y_offset) > 1024 ||
        e.cells.empty() || e.cells.size() > max_cells) throw ProtocolError("Host explosion bounds");
    const double radius = e.power * 2.0 + 2.0;
    const mining::Cell* previous = nullptr;
    for (const auto& c : e.cells) {
        if (!mining::valid_cell(c) || (previous && std::tie(c.x,c.y,c.z) <= std::tie(previous->x,previous->y,previous->z)))
            throw ProtocolError("Host explosion cell order");
        const double dx = c.x + .5 - e.center.x, dy = c.z + minecraft_y_offset + .5 - e.center.y,
                     dz = -c.y - .5 - e.center.z;
        if (dx*dx + dy*dy + dz*dz > radius*radius) throw ProtocolError("Host explosion cell reach");
        previous = &c;
    }
}
inline Bytes encode(const Event& e) {
    validate(e);
    Writer w; w.u64(e.epoch); w.u64(e.policy); w.u64(e.event);
    w.f32(e.center.x); w.f32(e.center.y); w.f32(e.center.z); w.f32(e.power);
    w.u32(static_cast<std::uint32_t>(e.cells.size()));
    for (const auto& c : e.cells) { w.u32(c.x); w.u32(c.y); w.u32(c.z); }
    return w.data;
}
inline Event decode(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); Event e; e.epoch=r.u64(); e.policy=r.u64(); e.event=r.u64();
    e.center={r.f32(),r.f32(),r.f32()}; e.power=r.f32(); const auto count=r.u32();
    if (!count || count > max_cells || r.remaining() != count*12u) throw ProtocolError("Host explosion cell count");
    e.cells.reserve(count);
    for (std::uint32_t i=0;i<count;++i) e.cells.push_back({static_cast<std::int32_t>(r.u32()),static_cast<std::int32_t>(r.u32()),static_cast<std::int32_t>(r.u32())});
    r.finish(); validate(e); return e;
}
inline bool permits(const Event& e, const mining::Policy& p, std::uint64_t last) noexcept {
    return e.epoch == p.epoch && e.policy == p.revision && e.event > last &&
        p.mode == mining::Mode::all_geometry && (p.capabilities & mining::geometry_carving);
}
} // namespace goldcraft::explosion
