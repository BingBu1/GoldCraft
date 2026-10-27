// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

#include <span>

namespace csol::scenario
{
struct TriggerRuleTable;
// Import raw .cso resource bytes from the game filesystem. The caller selects
// encrypted input explicitly; there is no reliable format magic to autodetect.
// Decode a private copy, trim only terminal NUL padding, then validate the CSV.
// Failure preserves both the original bytes and the previously loaded table.
bool ReadWaveResource(std::span<const std::byte> encoded, WaveTable& output, ConfigError& error);
bool ReadRoundResource(std::span<const std::byte> encoded, RoundTable& output, ConfigError& error);
bool ReadTriggerRuleResource(std::span<const std::byte> encoded, TriggerRuleTable& output, ConfigError& error);
}
