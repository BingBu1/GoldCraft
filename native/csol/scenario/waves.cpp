// SPDX-License-Identifier: GPL-3.0-or-later
#include "waves.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace csol::scenario
{
WaveRunner WaveRunner::Empty(int waveId)
{
    WaveRunner runner;
    runner.waveId_ = WaveKey(waveId);
    runner.loaded_ = true;
    return runner;
}

namespace
{
std::int32_t Add32(std::int32_t value, std::int32_t amount)
{
    return std::bit_cast<std::int32_t>(
        static_cast<std::uint32_t>(value) + static_cast<std::uint32_t>(amount));
}

bool AdvancePastMissedTimes(RowClock& clock, float elapsed)
{
    // Find the same first integer schedule time that the original addition loop
    // reaches. Compare float32 values: converting elapsed to an integer or using
    // ceil(elapsed-next) changes behavior above 2^24. Binary search bounds a long
    // missed interval without firing a burst or spending seconds catching up.
    const auto maximum = std::numeric_limits<std::int32_t>::max();
    const std::int64_t limit = (static_cast<std::int64_t>(maximum) - clock.next) / clock.interval;
    if (limit < 1) return false;
    const auto valueAt = [&](std::int64_t steps)
    {
        return static_cast<std::int32_t>(static_cast<std::int64_t>(clock.next) + steps * clock.interval);
    };
    if (static_cast<float>(valueAt(limit)) < elapsed) return false;
    std::int64_t low = 1, high = limit;
    while (low < high)
    {
        const auto mid = low + (high - low) / 2;
        if (static_cast<float>(valueAt(mid)) < elapsed) low = mid + 1;
        else high = mid;
    }
    clock.next = valueAt(low);
    return true;
}
}

RowStep AdvanceRow(RowClock& clock, float elapsed)
{
    if (clock.next == -1) return RowStep::Continue;
    if (!std::isfinite(elapsed) || clock.interval < 0) return RowStep::Invalid;
    if (static_cast<float>(clock.next) > elapsed) return RowStep::WaitForPoint;
    if (elapsed <= static_cast<float>(Add32(clock.next, 1)))
    {
        clock.next = clock.interval == 0 ? -1 : Add32(clock.next, clock.interval);
        return RowStep::Dispatch;
    }
    if (clock.interval == 0) clock.next = -1;
    else if (elapsed > static_cast<float>(clock.next) && !AdvancePastMissedTimes(clock, elapsed))
        return RowStep::Invalid; // An overflowing catch-up loop is not executable data.
    return RowStep::Continue;
}

void FinishRow(RowClock& clock, float elapsed)
{
    if (clock.end != 0 && elapsed >= static_cast<float>(clock.end)) clock.next = -1;
}

void ResetRunningRow(RowClock& clock, const WaveRow& original)
{
    if (clock.next == -1) return;
    clock = {original.startTime, original.endTime, original.interval};
}

bool RestartRowAt(const WaveRow& original, float offset, RowClock& clock)
{
    if (!std::isfinite(offset) || original.interval < 0) return false;
    const auto shifted = [offset](std::int32_t time, std::int32_t& result)
    {
        const float value = static_cast<float>(time) + offset;
        // cvttss2si truncates the rounded float32 sum. Its indefinite result
        // for overflow/NaN is not an executable schedule; reject it explicitly.
        if (!std::isfinite(value) || value < -2147483648.0f || value >= 2147483648.0f)
            return false;
        result = static_cast<std::int32_t>(value);
        return true;
    };
    RowClock candidate{0, 0, original.interval};
    if (!shifted(original.startTime, candidate.next) ||
        (original.endTime != 0 && !shifted(original.endTime, candidate.end))) return false;
    clock = candidate;
    return true;
}

bool SelectMonsterType(const WaveRow& row, int randomValue, int& type)
{
    if (randomValue < 0 || row.weightTotal <= 0 || row.types.size() != row.weights.size())
        return false;
    const int choice = randomValue % row.weightTotal;
    int sum = 0;
    for (std::size_t i = 0; i < row.weights.size(); ++i)
    {
        sum += row.weights[i];
        if (choice < sum)
        {
            type = row.types[i];
            return true;
        }
    }
    return false;
}

bool WaveRunner::Load(const WaveTable& table, int waveId, ConfigError& error)
{
    error = {};
    if (ticking_)
    {
        error.message = "cannot replace a wave during dispatch";
        return false;
    }
    WaveRunner candidate;
    candidate.waveId_ = WaveKey(waveId);
    std::map<int, std::size_t> pointIndices;
    for (const auto& row : table.rows)
    {
        if (WaveKey(row.waveId) != WaveKey(waveId)) continue;
        if (row.interval < 0 || row.types.empty())
        {
            if (row.recordIndex < table.source.records.size())
                error.byteOffset = table.source.records[row.recordIndex].byteOffset;
            error.message = "wave has a negative interval or an empty type list";
            return false;
        }
        const auto index = candidate.rows_.size();
        candidate.rows_.push_back(row);
        // MP applies this event delay once, to next only. A later running-row
        // reset restores the original unshifted start/end/interval fields.
        candidate.clocks_.push_back({row.IsEvent() && row.types.front() != 9999 ?
            Add32(row.startTime, 2) : row.startTime, row.endTime, row.interval});
        const auto [point, inserted] = pointIndices.try_emplace(row.spawnPoint, candidate.pointRows_.size());
        if (inserted) candidate.pointRows_.emplace_back();
        candidate.pointRows_[point->second].push_back(index);
    }
    if (candidate.rows_.empty())
    {
        error.message = "wave " + std::to_string(waveId) + " is missing";
        return false;
    }
    candidate.loaded_ = true;
    *this = std::move(candidate);
    return true;
}

WaveTickResult WaveRunner::Tick(float elapsed, Dispatch dispatch, void* context,
    Current current, const void* ownerContext)
{
    if (!loaded_ || ticking_ || !dispatch || !std::isfinite(elapsed) ||
        (current && !current(ownerContext))) return {};
    ticking_ = true;
    struct TickScope
    {
        bool& ticking;
        ~TickScope() { ticking = false; }
    } scope{ticking_};
    WaveTickResult result{true, true, 0};
    for (const auto& point : pointRows_)
    {
        for (const auto index : point)
        {
            if (current && !current(ownerContext)) { result.valid = false; result.finished = false; return result; }
            auto& clock = clocks_[index];
            const auto step = AdvanceRow(clock, elapsed);
            if (step == RowStep::Invalid)
            {
                result.valid = false;
                result.finished = false;
                return result;
            }
            if (step == RowStep::WaitForPoint)
            {
                result.finished = false;
                break;
            }
            if (step == RowStep::Dispatch)
            {
                ++result.attempts;
                dispatch(context, rows_[index], clock);
                if (current && !current(ownerContext)) { result.valid = false; result.finished = false; return result; }
            }
            // Creation failure does not roll the clock back. The handler may
            // modify clock fields, which MP also reads again for the end check.
            FinishRow(clock, elapsed);
            if (clock.next != -1) result.finished = false;
        }
    }
    return result;
}

bool WaveRunner::ResetRunningRows()
{
    if (!loaded_ || ticking_) return false;
    for (std::size_t i = 0; i < rows_.size(); ++i) ResetRunningRow(clocks_[i], rows_[i]);
    return true;
}

bool WaveRunner::RestartAt(float offset)
{
    if (!loaded_ || ticking_ || !std::isfinite(offset)) return false;
    std::vector<RowClock> clocks;
    clocks.reserve(rows_.size());
    for (const auto& row : rows_)
    {
        RowClock clock;
        if (!RestartRowAt(row, offset, clock)) return false;
        clocks.push_back(clock);
    }
    clocks_ = std::move(clocks);
    startOffset_ = offset;
    return true;
}
}
