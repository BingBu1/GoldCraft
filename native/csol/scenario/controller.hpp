// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "waves.hpp"

#include <map>

namespace csol::scenario
{
class WaveController
{
public:
    // Activates the ordinary round's wave IDs, retaining order and duplicates.
    // An absent ID is an empty program, as in MP; it remains visible in the
    // diagnostic list and is removed from active IDs by the first valid tick.
    // Malformed existing programs fail transactionally, preserving the old round.
    bool Activate(const WaveTable& table, std::span<const std::uint16_t> waveIds,
        ConfigError& error);
    // Triggered rounds preload their group's w references. None runs until a
    // trigger activates it; the group ID is never an active wave ID.
    bool Prepare(const WaveTable& table, std::span<const std::uint16_t> waveIds,
        ConfigError& error);
    WaveTickResult Tick(float elapsed, WaveRunner::Dispatch dispatch, void* context);
    // Spatial/direct trigger activation appends without deduplicating and
    // restarts the shared program at the current scenario elapsed time.
    // An ID absent from the preloaded runtime remains empty, even if another
    // round's global source table contains it. Activation never loads source data.
    bool ActivateWave(std::uint16_t id, float elapsed, ConfigError& error);
    bool EndWave(std::uint16_t id);

    std::span<const std::uint16_t> ActiveWaves() const { return active_; }
    std::span<const std::uint16_t> MissingWaves() const { return missing_; }
    const WaveRunner* FindWave(std::uint16_t id) const;
    bool IsDispatching() const { return ticking_; }

private:
    bool Load(const WaveTable& table, std::span<const std::uint16_t> waveIds,
        bool activate, ConfigError& error);
    bool loaded_{};
    bool ticking_{};
    std::vector<std::uint16_t> active_;
    std::vector<std::uint16_t> missing_;
    std::map<std::uint16_t, WaveRunner> waves_;
};
}
