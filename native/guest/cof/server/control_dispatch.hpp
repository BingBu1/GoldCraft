// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
class CBasePlayer;
namespace guest::cof
{
struct ControlCall { void* context{}; bool (*current)(void*) noexcept {}; };
enum class ControlStatus : unsigned char { Sent, InvalidInput, MissingEngine, MissingMessage, Cancelled, Busy };
// Cold authoritative-thread registration; keep this ID with the map generation.
int RegisterControlMessage(const ControlCall&) noexcept;
// Original WpnDeploy is unreliable, WeaponBody reliable. Sent means packet
// completion, never client acknowledgment, resource readiness or forward clear.
ControlStatus SendWeaponDeploy(CBasePlayer&, int message, const ControlCall&) noexcept;
ControlStatus SendShotState(CBasePlayer&, std::uint8_t amount, int message, const ControlCall&) noexcept;
ControlStatus SendWeaponBody(CBasePlayer&, std::uint8_t body, int message, const ControlCall&) noexcept;
}
