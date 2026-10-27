// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "monster_sequences.hpp"

namespace csol::scenario
{
// A null cache represents the original null type. Headshots use the Head slot;
// all other hits consume exactly one CRT rand()%8, including forced clear.
int SelectOrdinaryMonsterDeathSequence(const MonsterSequences* sequences, int hitgroup);

struct MonsterDeathTiming
{
    float now{};
    // Already adjusted by the behavior. Ordinary behavior returns the exact
    // appearance duration; Boss overrides belong to their real implementation.
    float adjustedSeconds{};
    bool shortDeath{};
    int scenarioType{};
    std::int16_t skill{};
    std::int8_t priority{};
    int mapId{};
};

// Ordered original overrides, with float32 additions. Zero and past deadlines
// are legal; nonfinite final values fail without changing the output.
bool ResolveMonsterDeathDeadline(const MonsterDeathTiming& timing, float& deadline);
}
