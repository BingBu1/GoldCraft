#pragma once
#include "goldcraft/wire.hpp"
#include <cmath>
#include <limits>

namespace goldcraft::mining {
enum class Mode : std::uint32_t { disabled = 0, damageable_entities = 1, all_geometry = 2 };
enum Capability : std::uint32_t { entity_damage = 1, geometry_carving = 2 };
enum class Target { none, damageable_entity, geometry };
enum class Status : std::uint32_t {
    applied = 0, disabled = 1, stale_policy = 2, stale_actor = 3,
    replay = 4, cooldown = 5, invalid_target = 6, obstructed = 7,
    no_effect = 8, geometry_unavailable = 9
};

inline Mode cvar_mode(float value) noexcept {
    if (value == 1.0f) return Mode::damageable_entities;
    if (value == 2.0f) return Mode::all_geometry;
    return Mode::disabled;
}
inline bool permits(Mode mode, Target target) noexcept {
    return target != Target::none && (mode == Mode::all_geometry ||
        (mode == Mode::damageable_entities && target == Target::damageable_entity));
}

struct Policy {
    std::uint64_t epoch = 0, revision = 0;
    Mode mode = Mode::disabled;
    std::uint32_t capabilities = 0;
    bool operator==(const Policy&) const = default;
};
inline Bytes encode_policy(const Policy& p) {
    Writer w; w.u64(p.epoch); w.u64(p.revision);
    w.u32(static_cast<std::uint32_t>(p.mode)); w.u32(p.capabilities); return w.data;
}
inline Policy decode_policy(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); Policy p; p.epoch = r.u64(); p.revision = r.u64();
    const auto mode = r.u32(); p.mode = static_cast<Mode>(mode); p.capabilities = r.u32(); r.finish();
    if (!p.epoch || !p.revision || mode > 2 || (p.capabilities & ~3u)) throw ProtocolError("Mining policy bounds");
    return p;
}

// This message is produced only by the authenticated Minecraft server. Clients
// submit a held-button intent; neither target identity nor damage is client data.
struct Request {
    std::uint64_t epoch = 0, revision = 0, event = 0;
    std::uint32_t slot = 0, serial = 0, life = 0;
    Key uuid{};
    std::uint32_t target = 0, target_serial = 0, model = 0;
    Vec3 point{};
    float damage = 0, reach = 0;
};
inline Bytes encode_request(const Request& q) {
    Writer w; w.u64(q.epoch); w.u64(q.revision); w.u64(q.event);
    w.u32(q.slot); w.u32(q.serial); w.u32(q.life); w.bytes(q.uuid);
    w.u32(q.target); w.u32(q.target_serial); w.u32(q.model);
    w.f32(q.point.x); w.f32(q.point.y); w.f32(q.point.z); w.f32(q.damage); w.f32(q.reach); return w.data;
}
inline Request decode_request(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); Request q; q.epoch = r.u64(); q.revision = r.u64(); q.event = r.u64();
    q.slot = r.u32(); q.serial = r.u32(); q.life = r.u32(); q.uuid = r.key();
    q.target = r.u32(); q.target_serial = r.u32(); q.model = r.u32();
    q.point = {r.f32(), r.f32(), r.f32()}; q.damage = r.f32(); q.reach = r.f32(); r.finish();
    if (!q.epoch || !q.revision || !q.event || !q.slot || q.slot > 64 || !q.serial || !q.life ||
        q.target > 32767 || q.model > 4095 || (q.target == 0 && (q.model || q.target_serial)) ||
        (q.target != 0 && q.model == 0) || q.damage <= 0 || q.damage > 5000 || q.reach <= 0 || q.reach > 192 ||
        std::abs(q.point.x) > 16384 || std::abs(q.point.y) > 16384 || std::abs(q.point.z) > 16384)
        throw ProtocolError("Mining request bounds");
    return q;
}

struct Result {
    std::uint64_t epoch = 0, event = 0, revision = 0;
    std::uint32_t slot = 0, serial = 0, life = 0, target = 0, target_serial = 0;
    Status status = Status::stale_actor;
    float before = 0, after = 0;
};
inline Bytes encode_result(const Result& result) {
    Writer w; w.u64(result.epoch); w.u64(result.event); w.u64(result.revision);
    w.u32(result.slot); w.u32(result.serial); w.u32(result.life); w.u32(result.target); w.u32(result.target_serial);
    w.u32(static_cast<std::uint32_t>(result.status)); w.f32(result.before); w.f32(result.after); return w.data;
}
inline Result decode_result(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); Result result; result.epoch = r.u64(); result.event = r.u64(); result.revision = r.u64();
    result.slot = r.u32(); result.serial = r.u32(); result.life = r.u32(); result.target = r.u32(); result.target_serial = r.u32();
    const auto status = r.u32(); result.status = static_cast<Status>(status); result.before = r.f32(); result.after = r.f32(); r.finish();
    if (!result.epoch || !result.event || !result.revision || !result.slot || result.slot > 64 || !result.serial || !result.life ||
        result.target > 32767 || status > static_cast<std::uint32_t>(Status::geometry_unavailable)) throw ProtocolError("Mining result bounds");
    return result;
}
}
