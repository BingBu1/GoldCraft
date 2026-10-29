// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace csol::scenario
{
bool WaveController::Activate(const WaveTable& table,
    std::span<const std::uint16_t> waveIds, ConfigError& error)
{
    return Load(table, waveIds, true, error);
}

bool WaveController::Prepare(const WaveTable& table,
    std::span<const std::uint16_t> waveIds, ConfigError& error)
{
    return Load(table, waveIds, false, error);
}

bool WaveController::Load(const WaveTable& table,
    std::span<const std::uint16_t> waveIds, bool activate, ConfigError& error)
{
    error = {};
    if (ticking_)
    {
        error.message = "cannot activate a round during wave dispatch";
        return false;
    }
    std::set<std::uint16_t> present;
    for (const auto& row : table.rows)
        present.insert(WaveKey(row.waveId));
    WaveController candidate;
    if (activate) candidate.active_.assign(waveIds.begin(), waveIds.end());
    for (const auto id : waveIds)
    {
        if (!present.contains(id)) candidate.missing_.push_back(id);
        if (candidate.waves_.contains(id)) continue;
        WaveRunner runner;
        if (present.contains(id))
        {
            if (!runner.Load(table, id, error)) return false;
        }
        else runner = WaveRunner::Empty(id);
        candidate.waves_.emplace(id, std::move(runner));
    }
    candidate.loaded_ = true;
    *this = std::move(candidate);
    return true;
}

bool WaveController::ActivateWave(std::uint16_t id, float elapsed, ConfigError& error)
{
    error = {};
    if (!loaded_ || ticking_ || !std::isfinite(elapsed))
    {
        error.message = "cannot activate a trigger wave in the current controller state";
        return false;
    }
    const auto found = waves_.find(id);
    const bool missing = found == waves_.end();
    auto runner = missing ? WaveRunner::Empty(id) : found->second;
    if (!runner.RestartAt(elapsed))
    {
        error.message = "trigger wave time is outside the supported signed 32-bit schedule";
        return false;
    }
    waves_[id] = std::move(runner);
    if (missing) missing_.push_back(id);
    active_.push_back(id);
    return true;
}

bool WaveController::EndWave(std::uint16_t id)
{
    if (!loaded_ || ticking_) return false;
    const auto found = std::find(active_.begin(), active_.end(), id);
    if (found == active_.end()) return false;
    active_.erase(found); // End only the first match; duplicate activations stay.
    return true;
}

WaveTickResult WaveController::Tick(float elapsed, WaveRunner::Dispatch dispatch, void* context,
    WaveRunner::Current current, const void* ownerContext)
{
    if (!loaded_ || ticking_ || !dispatch || !std::isfinite(elapsed) ||
        (current && !current(ownerContext))) return {};
    ticking_ = true;
    struct TickScope
    {
        bool& ticking;
        ~TickScope() { ticking = false; }
    } scope{ticking_};
    WaveTickResult result{true, false, 0};
    std::size_t index = 0;
    while (index < active_.size())
    {
        if (current && !current(ownerContext)) { result.valid = false; return result; }
        const auto id = active_[index];
        const auto found = waves_.find(id);
        WaveTickResult tick{true, true, 0};
        if (found != waves_.end()) tick = found->second.Tick(elapsed, dispatch, context, current, ownerContext);
        result.attempts += tick.attempts;
        if (!tick.valid)
        {
            result.valid = false;
            return result;
        }
        if (tick.finished)
        {
            // MP erases the first matching active ID and keeps the iteration
            // index. Duplicate IDs share the same mutable WaveRunner above.
            active_.erase(std::find(active_.begin(), active_.end(), id));
        }
        else ++index;
    }
    result.finished = active_.empty();
    return result;
}

const WaveRunner* WaveController::FindWave(std::uint16_t id) const
{
    const auto found = waves_.find(id);
    return found == waves_.end() ? nullptr : &found->second;
}
}
