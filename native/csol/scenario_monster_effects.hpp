// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::monster_effects
{
// CSO's engine-private TE128 is not a stock GoldSrc service. Carry its
// supported subtype through a bounded user message instead.
inline constexpr const char* Message = "CsoMonFX";
inline constexpr int Impact = 1;
inline constexpr int TrapDirection = 2;
inline constexpr int ZombieBomb = 3;
// Server-confirmed direct monster hits: subtype, amount, three coordinates.
inline constexpr int Blood = 4;
inline constexpr int Spark = 5;
inline constexpr int HitPayloadSize = 8;
inline constexpr const char* ImpactSprite = "sprites/deimosexp.spr";
inline constexpr const char* ZombieBombSprite = "sprites/zombiebomb.spr";
inline constexpr const char* ImpactSound = "zombi/Zombi_Bomb_Exp.wav";
}
