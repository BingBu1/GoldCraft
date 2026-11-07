// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace csol::wall_upgrade
{
inline constexpr char Classname[] = "zs_wallupmachine";
inline constexpr char Model[] = "models/ze_machine.mdl";
inline constexpr char Sprite[] = "sprites/e_button01.spr";
inline constexpr char DeniedText[] = "#CSO_ZBS_CantUpgradeWall";
inline constexpr int Price = 5000;
inline constexpr std::int8_t InitialLevel = 1, MaximumLevel = 28;
inline constexpr int StatusType = 9, ExternalEvent = 51;
inline constexpr std::array<float, 3> Origin{-166, 212, 145}, Angles{0, -90, 0};
inline constexpr std::array<float, 3> IndicatorOffset{0, 0, 20};
inline constexpr float FirstThink = 5, ThinkInterval = 11, IndicatorDuration = 10,
    IndicatorScale = .2f;

enum class Eligibility { Allowed, Maximum, InsufficientMoney, Participation };

// MP114C0140: preserve the original early-return order. CT wins is the
// authoritative root+278 value, not a guessed wave or elapsed-time test.
constexpr Eligibility Check(std::int8_t level, int account, std::int16_t ctWins,
    std::uint8_t participation)
{
    if (level >= MaximumLevel) return Eligibility::Maximum;
    if (account < Price) return Eligibility::InsufficientMoney;
    if (ctWins != 0 && participation != 2) return Eligibility::Participation;
    return Eligibility::Allowed;
}

// MP11382C94..CC8, jump table11382E30/38. The caller supplies only actual
// connected, non-dormant, non-spectator participants from BeginRound.
constexpr std::uint8_t BeginRound(std::uint8_t previous, int subtype)
{
    switch (subtype)
    {
    case 1: case 2: case 3: case 4: case 6: case 10: return 2;
    default: return previous == 0 ? 1 : previous == 1 ? 2 : previous;
    }
}
}
