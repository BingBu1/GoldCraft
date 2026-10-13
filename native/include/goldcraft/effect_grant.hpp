#pragma once
#include "goldcraft/wire.hpp"
#include "goldcraft/effect_registry.hpp"
#include <limits>

namespace goldcraft::effects {
inline constexpr std::uint32_t grant_schema = 1;
inline constexpr std::size_t grant_bytes = 164, effect_pose_bytes = 128, reopen_bytes = 64;
enum class GrantOperation : std::uint32_t { open = 1, update = 2 };
struct Grant {
    GrantOperation operation = GrantOperation::open;
    std::uint64_t serviceGeneration = 0, nativeConnection = 0;
    Snapshot constraints{};
    Key minecraftUuid{};
    std::uint64_t stream = 0, publication = 0, frequency = 0, issuedAtMs = 0, deadlineMs = 0;
    bool bootstrap = false;
    std::array<float, 3> nativeFeet{}, nativeAnchorFeet{};
    bool operator==(const Grant&) const = default;
};
struct PoseProof {
    std::uint64_t serviceGeneration = 0, nativeConnection = 0, stream = 0, publication = 0;
    bool empty() const noexcept { return !serviceGeneration && !nativeConnection && !stream && !publication; }
};
struct Reopen {
    std::uint64_t world = 0;
    std::uint32_t slot = 0, serial = 0, life = 0;
    Key minecraftUuid{};
    std::uint64_t serviceGeneration = 0, nativeConnection = 0, stream = 0;
};
inline bool valid_grant(const Grant& g) noexcept {
    constexpr auto signed_max = static_cast<std::uint64_t>(INT64_MAX);
    if ((g.operation != GrantOperation::open && g.operation != GrantOperation::update) ||
        !g.serviceGeneration || !g.nativeConnection || !g.stream || !g.publication ||
        !valid_target(g.constraints.target) || g.constraints.target.kind != TargetKind::player ||
        !g.constraints.revision || !valid_movement(g.constraints.movement) || g.minecraftUuid == Key{} ||
        !g.frequency || g.frequency > signed_max || g.issuedAtMs > signed_max ||
        g.deadlineMs > signed_max || g.deadlineMs <= g.issuedAtMs || g.deadlineMs - g.issuedAtMs > 60000)
        return false;
    for (const auto& point : {g.nativeFeet, g.nativeAnchorFeet})
        for (float v : point) if (!std::isfinite(v) || std::abs(v) > 16384.0f) return false;
    return true;
}
inline Bytes encode_grant(const Grant& g) {
    if (!valid_grant(g)) throw ProtocolError("Invalid effect grant");
    Writer w; w.data.reserve(grant_bytes);
    w.u32(grant_schema); w.u32(static_cast<std::uint32_t>(g.operation));
    w.u64(g.serviceGeneration); w.u64(g.nativeConnection);
    const auto& t = g.constraints.target;
    w.u64(t.world); w.u32(static_cast<std::uint32_t>(t.kind)); w.u64(t.key); w.u32(t.serial); w.u32(t.life);
    w.bytes(g.minecraftUuid); w.u64(g.stream); w.u64(g.publication); w.u64(g.constraints.revision);
    w.u64(g.frequency); w.u64(g.issuedAtMs); w.u64(g.deadlineMs); w.u32(g.bootstrap ? 1 : 0);
    for (float v : g.nativeFeet) w.f32(v);
    for (float v : g.nativeAnchorFeet) w.f32(v);
    const auto& m = g.constraints.movement;
    w.u32(m.capability); w.u32(m.inputLocks); w.u32(m.holdAxes); w.f32(m.wishSpeedCap); w.f32(m.gravityFactor);
    return std::move(w.data);
}
inline Grant decode_grant(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != grant_bytes) throw ProtocolError("Invalid effect grant size");
    Reader r(bytes); Grant g;
    if (r.u32() != grant_schema) throw ProtocolError("Invalid effect grant schema");
    g.operation = static_cast<GrantOperation>(r.u32());
    g.serviceGeneration = r.u64(); g.nativeConnection = r.u64();
    auto& t = g.constraints.target;
    t.world = r.u64(); t.kind = static_cast<TargetKind>(r.u32()); t.key = r.u64(); t.serial = r.u32(); t.life = r.u32();
    g.minecraftUuid = r.key(); g.stream = r.u64(); g.publication = r.u64(); g.constraints.revision = r.u64();
    g.frequency = r.u64(); g.issuedAtMs = r.u64(); g.deadlineMs = r.u64();
    const auto bootstrap = r.u32();
    if (bootstrap > 1) throw ProtocolError("Invalid effect bootstrap flag");
    g.bootstrap = bootstrap != 0;
    for (float& v : g.nativeFeet) v = r.f32();
    for (float& v : g.nativeAnchorFeet) v = r.f32();
    auto& m = g.constraints.movement;
    m.capability = r.u32(); m.inputLocks = r.u32(); m.holdAxes = r.u32(); m.wishSpeedCap = r.f32(); m.gravityFactor = r.f32();
    r.finish();
    if (!valid_grant(g)) throw ProtocolError("Invalid effect grant");
    return g;
}
inline PoseProof decode_pose_proof(Reader& r) {
    if (r.remaining() != effect_pose_bytes || r.u32() != grant_schema) throw ProtocolError("Invalid effect pose envelope");
    PoseProof p{r.u64(), r.u64(), r.u64(), r.u64()};
    if (!p.empty() && (!p.serviceGeneration || !p.nativeConnection || !p.stream || !p.publication))
        throw ProtocolError("Partial effect pose proof");
    return p;
}
inline Reopen decode_reopen(Reader& r) {
    if (r.remaining() != reopen_bytes || r.u32() != grant_schema) throw ProtocolError("Invalid effect reopen envelope");
    Reopen p; p.world = r.u64(); p.slot = r.u32(); p.serial = r.u32(); p.life = r.u32(); p.minecraftUuid = r.key();
    p.serviceGeneration = r.u64(); p.nativeConnection = r.u64(); p.stream = r.u64(); r.finish();
    if (!p.world || !p.slot || p.slot > 64 || !p.serial || !p.life || p.minecraftUuid == Key{} ||
        !p.serviceGeneration || !p.nativeConnection || !p.stream) throw ProtocolError("Invalid effect reopen identity");
    return p;
}
}
