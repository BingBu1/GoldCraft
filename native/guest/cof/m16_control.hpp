// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "magazine_actions.hpp"

namespace guest::cof
{
struct M16ControlState
{
    MagazineActionState action;
    bool burst{true};
};
enum class M16ControlEventKind : unsigned char
{
    Shoot, EmptyClick, Reload, ViewAnimation, PlayerReloadLoaded, PlayerReloadEmpty
};
struct M16ControlEvent
{
    M16ControlEventKind kind;
    int sequence{};
};
struct M16ControlHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    // Pure authoritative clock read. Events may advance time or retire the
    // owner, so the original post-consumer reads must not reuse entry time.
    float (*time)(void*) noexcept {};
    bool (*event)(void*, const M16ControlEvent&) noexcept {};
};
enum class M16ControlResult : unsigned char
{
    Applied, Deferred, Ineligible, MissingHost, MissingSequence, InvalidInput,
    ConsumerRejected, Cancelled, UnsupportedState
};

// Matched hl.dll10051D50. Shoot is the complete per-shot consumer, including
// its own eligibility and scheduling; this wrapper only starts the group.
M16ControlResult M16PrimaryTrigger(M16ControlState&, bool attackPressed,
    bool fireOnEmpty, const M16ControlHost&) noexcept;
// Matched10052D20: choose animation from the old mode, then toggle it.
M16ControlResult M16ToggleFireMode(M16ControlState&, const M16ControlHost&) noexcept;
// Matched10051ED0 and1011C560/10120210. Skill-expanded host magazines are
// retained until spent; requesting reload must not discard their extra ammo.
M16ControlResult StartM16MagazineReload(M16ControlState&, const MagazineSequenceTimes&,
    const M16ControlHost&) noexcept;

struct M16FrameOwner
{
    float velocityX{}, velocityY{}, pendingGateTime{};
    std::uint32_t flags{};
    bool condition3AA{}, pendingGate{};
};
enum class M16FrameEvent : unsigned char { ViewAnimation, ExitAim, Reload };
struct M16FrameHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    // Pure authoritative reads, refreshed at the original instruction boundaries.
    bool (*owner)(void*, M16FrameOwner&) noexcept {};
    bool (*event)(void*, M16FrameEvent, int sequence) noexcept {};
    // Resolve the current owner weaponanim after ViewAnimation, rather than
    // assuming its requested sequence survived the transport callback.
    bool (*duration)(void*, float&) noexcept {};
};
// Original10051540 ordinary derived frame. The caller runs common1011D560
// afterwards. Special1/2 lifecycle is not admitted.
M16ControlResult AdvanceM16DerivedFrame(MagazineActionState&, float& actionDeadline,
    const M16FrameHost&) noexcept;

struct M16InputOwner
{
    std::uint32_t buttons{};
    float activityBlock{};
    int moveType{};
};
enum class M16InputEvent : unsigned char
{
    InventoryChanged, Primary, Secondary, Aim, Reload
};
struct M16InputHost
{
    void* context{};
    bool (*current)(void*) noexcept {};
    float (*time)(void*) noexcept {};
    bool (*owner)(void*, M16InputOwner&) noexcept {};
    bool (*event)(void*, M16InputEvent) noexcept {};
};
// Original1011D9BC onward for the M16 virtual table. Call after the original
// common reload-commit stage; weapon actions retain their own eligibility.
M16ControlResult DispatchM16FrameInput(const MagazineActionState&, int& fireOnEmpty,
    const M16InputHost&) noexcept;
}
