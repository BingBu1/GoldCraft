// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sound_dispatch.hpp"
#include "../presentation_protocol.hpp"
#include <cstdint>

namespace guest::cof
{
enum class WeaponEffectStatus : unsigned char
{
    Issued, InvalidInput, MissingEngine, MissingMessage, Cancelled, Busy
};
struct ShellRequest
{
    Vector origin{}, velocity{};
    float angle{};
    int model{}, bounce{};
};

// Actual engine consumers, with no allocation, lookup or resource I/O. The
// authority context must survive the entire call, including packet completion.
// Register/prepare the real event/model/message consumers before using these;
// dispatch alone never establishes whole-weapon/client readiness.
WeaponEffectStatus EjectWeaponShell(const ShellRequest&, const PlaySfxCall&) noexcept;
// Only the original rules-v20 true branch. A single-player punchangle/vuser
// substitute is deliberately not supplied by this multiplayer integration.
WeaponEffectStatus CooperativeViewRecoil(CBasePlayer&, const Vector& impulse,
    unsigned short preparedEvent, const PlaySfxCall&) noexcept;
struct RevolverShellEvent
{
    Vector origin{};
    float delay{};
    int oldClip{};
};
// Original delayed revolvershells event; its client consumer plays sound only.
// The original reload loop supplies each delay, never a second host timer.
WeaponEffectStatus PlayRevolverShellEvent(CBasePlayer&, const RevolverShellEvent&,
    unsigned short preparedEvent, const PlaySfxCall&) noexcept;
WeaponEffectStatus SendLocalGunsmoke(CBasePlayer&, std::uint8_t originalWeapon,
    std::int16_t weaponEntity, int preparedMessage, const PlaySfxCall&) noexcept;
// Cold registration and actual owning-player transport for the authenticated
// FOV/aim bridge. Key/revision management belongs to the authoritative owner.
int RegisterPresentationMessage(const PlaySfxCall&) noexcept;
WeaponEffectStatus SendPresentationState(CBasePlayer&, const PresentationState&,
    int preparedMessage, const PlaySfxCall&) noexcept;
}
