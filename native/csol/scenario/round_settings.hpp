// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

namespace csol::scenario
{
struct RoundSettings
{
    std::uint8_t music{};
    std::int16_t playerSpawn{};
};

// These are positional columns 10/11 of the original round row. PlayerSpawn
// is independent of the phase index, round number and trigger/wave groups.
// Missing/empty cells read as zero. Failure leaves the previous output intact.
bool ReadRoundSettings(const RoundTable& table, std::size_t index,
    RoundSettings& output, ConfigError& error);
// Original manager+284: integer cell column1/row5, independent of the
// per-round row and its music/spawn settings. Zero disables reward markers.
bool ReadRewardRound(const RoundTable& table, std::int32_t& output, ConfigError& error);
}
