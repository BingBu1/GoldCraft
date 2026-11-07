// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.hpp"

#include <span>

namespace csol::scenario
{
struct RowClock
{
    std::int32_t next{};
    std::int32_t end{};
    std::int16_t interval{};
};

enum class RowStep
{
    WaitForPoint = 0, // Skip the remaining rows of this point for this tick.
    Continue = 1,     // Ended row or late row advanced without an attempt.
    Dispatch = 2,    // next was advanced before calling the game handler.
    Invalid = 3
};

RowStep AdvanceRow(RowClock& clock, float elapsed);
void FinishRow(RowClock& clock, float elapsed);
void ResetRunningRow(RowClock& clock, const WaveRow& original);
bool RestartRowAt(const WaveRow& original, float offset, RowClock& clock);

// The supplied random value comes from the game's RNG. This function preserves
// the original modulo/weight selection, not the RNG state or seed algorithm.
bool SelectMonsterType(const WaveRow& row, int randomValue, int& type);

struct WaveTickResult
{
    bool valid{};
    bool finished{};
    std::size_t attempts{};
};

class WaveRunner
{
public:
    using Dispatch = void (*)(void* context, const WaveRow& row, RowClock& clock);
    using Current = bool (*)(const void* context); // No callbacks; owner/error gate, not the wave-stop flag.

    // Builds one wave in first-seen point order, retaining every row. Failure
    // leaves the previous program intact; no missing wave is synthesized.
    bool Load(const WaveTable& table, int waveId, ConfigError& error);
    // Original runtime-map lookup creates an empty program for absent IDs.
    // It still has an identity/offset and completes on its next valid tick.
    static WaveRunner Empty(int waveId);
    WaveTickResult Tick(float elapsed, Dispatch dispatch, void* context,
        Current current = nullptr, const void* ownerContext = nullptr);
    bool ResetRunningRows();
    // Trigger activation resets every row, including rows that already ended.
    // Original event-load +2 is not applied again by this distinct operation.
    bool RestartAt(float offset);

    int WaveId() const { return waveId_; }
    std::span<const WaveRow> Rows() const { return rows_; }
    std::span<const RowClock> Clocks() const { return clocks_; }
    float StartOffset() const { return startOffset_; }

private:
    int waveId_{};
    bool loaded_{};
    bool ticking_{};
    float startOffset_{};
    std::vector<WaveRow> rows_;
    std::vector<RowClock> clocks_;
    std::vector<std::vector<std::size_t>> pointRows_;
};
}
