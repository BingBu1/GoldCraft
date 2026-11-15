// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace goldcraft::resource_timing {
// Per-owner counters: workers publish theirs only with the completed job.
// Wall time is inclusive; parallel worker totals are not admission latency.
struct Counter {
    std::uint64_t calls{};
    double totalMs{}, maxMs{};
    void Add(double milliseconds) { ++calls; totalMs += milliseconds; maxMs = std::max(maxMs, milliseconds); }
    void Merge(const Counter& other) { calls += other.calls; totalMs += other.totalMs; maxMs = std::max(maxMs, other.maxMs); }
};
class Scope {
    Counter& counter_;
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
public:
    explicit Scope(Counter& counter) : counter_(counter) {}
    ~Scope() { counter_.Add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count()); }
    Scope(const Scope&) = delete;
};
template<class Function> decltype(auto) Measure(Counter& counter, Function&& function) {
    Scope scope(counter);
    return std::forward<Function>(function)();
}
}
