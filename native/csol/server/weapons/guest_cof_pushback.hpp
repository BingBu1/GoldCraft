// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native/guest/cof/pushback.hpp"
#include "native/guest/cof/server/sound_dispatch.hpp"
#include <array>
#include <memory>

class CBasePlayer;
class CCSOLScenarioRuntime;
namespace csol::server::cof
{
struct NativePushbackHost
{
    guest::cof::PlaySfxCall authority;
    bool (*stamina)(void*, float& amount, bool& infinite) noexcept {};
    bool (*setStamina)(void*, float requestedAmount) noexcept {};
    std::array<const char*, 2> samples{}; // Cold-prepared swing/hit, in that order.
    unsigned short recoilEvent{};
    float damage{}; // Authored scenario melee value, independent of the held gun.
    bool quietAudio{};
};
// Actual engine sphere traversal and native damage, on the authoritative game
// thread. The original player task owns timing; this call owns no weapon or AI.
guest::cof::PushbackResult NativeLongGunPushback(CBasePlayer&,
    const std::shared_ptr<CCSOLScenarioRuntime>&, const NativePushbackHost&) noexcept;
}
