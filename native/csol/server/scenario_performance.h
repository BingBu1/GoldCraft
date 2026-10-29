// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Main server thread only. These diagnostics never drive game scheduling.
// Timers are inclusive: nested categories MUST NOT be added together.
namespace csol::scenario::perf
{
enum class Timer : std::size_t
{
    Think, Behavior, Position, Follow, Route, Search, Ground, Nearest, Diagnostics, Count
};
enum class Counter : std::size_t
{
    Rebuild, RouteFailed, RouteCancelled, SearchExpanded, SearchEdges,
    SearchInsert, SearchDecrease, SearchReached, SearchPartial,
    AreaCandidates, NearestCandidates, GroundTraces, SimpleGroundTraces,
    NearestTraces, FollowTraces, Count
};
inline constexpr std::array<std::string_view, std::size_t(Timer::Count)> TimerNames{
    "think", "behavior", "position", "follow", "route", "search", "ground", "nearest", "diagnostics"};
inline constexpr std::array<std::string_view, std::size_t(Counter::Count)> CounterNames{
    "rebuild", "route_failed", "route_cancelled", "search_expanded", "search_edges",
    "search_insert", "search_decrease", "search_reached", "search_partial",
    "area_candidates", "nearest_candidates", "ground_traces", "simple_ground_traces",
    "nearest_traces", "follow_traces"};
struct Sample { std::uint64_t calls{}, nanoseconds{}, maximum{}; };
struct Measurements
{
    std::array<Sample, std::size_t(Timer::Count)> timers{};
    std::array<std::uint64_t, std::size_t(Counter::Count)> counters{};
};
inline bool enabled{};
inline std::uint64_t generation{1};
inline Measurements measurements;
using Clock = std::chrono::steady_clock;

inline void Reset(bool enable)
{
    ++generation;
    enabled = enable;
    measurements = {};
}
inline void Add(Counter counter, std::uint64_t count = 1)
{
    if (enabled) measurements.counters[std::size_t(counter)] += count;
}
class Scope final
{
public:
    explicit Scope(Timer timer) : m_timer(timer), m_generation(enabled ? generation : 0)
    {
        if (m_generation) m_start = Clock::now();
    }
    ~Scope()
    {
        if (!m_generation || !enabled || generation != m_generation) return;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_start).count();
        auto& sample = measurements.timers[std::size_t(m_timer)];
        const auto elapsed = ns > 0 ? static_cast<std::uint64_t>(ns) : 0;
        ++sample.calls;
        sample.nanoseconds += elapsed;
        if (elapsed > sample.maximum) sample.maximum = elapsed;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Timer m_timer;
    std::uint64_t m_generation{};
    Clock::time_point m_start{};
};
}

void CSOL_InitScenarioPerformance();
void CSOL_RecordScenarioPerformanceFrame();
// Disabled path is one inline branch: no clock reads, scans or allocations.
inline void CSOL_ScenarioPerformanceFrame()
{
    if (csol::scenario::perf::enabled) CSOL_RecordScenarioPerformanceFrame();
}
