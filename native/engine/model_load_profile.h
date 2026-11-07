// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>

namespace goldcraft::model_profile
{
enum class Stage { FileRead, Crc, Decode, DecodeSafety, Prepare, Storage, Copy, Commit, Count };
inline constexpr std::array names{"file_read", "crc", "decode", "decode_safety",
    "prepare", "storage", "copy", "commit"};
struct Counter { double seconds{}; unsigned calls{}; };
inline std::array<Counter, static_cast<std::size_t>(Stage::Count)> counters{};
inline std::uint64_t generation{};
inline bool enabled{};

// No stack pointer survives a Host_Error longjmp. A new phase invalidates any
// old diagnostic scope; only a normally completed current phase is reported.
class Phase
{
public:
    explicit Phase(const char* name) : name_(name), generation_(++generation),
        exceptions_(std::uncaught_exceptions())
    {
        counters = {};
        const auto* setting = std::getenv("GOLDCRAFT_MAP_LOAD_TIMING");
        enabled = setting && setting[0] == '1' && setting[1] == '\0';
    }
    ~Phase()
    {
        if (generation_ != generation) return;
        if (enabled && std::uncaught_exceptions() == exceptions_)
            for (std::size_t i = 0; i < counters.size(); ++i)
                if (counters[i].calls)
                    Log_Printf("GoldCraft modelload phase=%s stage=%s calls=%u seconds=%.6f\n",
                        name_, names[i], counters[i].calls, counters[i].seconds);
        enabled = false;
    }
    Phase(const Phase&) = delete;
    Phase& operator=(const Phase&) = delete;
private:
    const char* name_;
    std::uint64_t generation_;
    int exceptions_;
};

class Scope
{
public:
    explicit Scope(Stage stage) : stage_(stage), generation_(enabled ? generation : 0)
    {
        if (generation_) start_ = Sys_FloatTime();
    }
    ~Scope()
    {
        if (!generation_ || generation_ != generation || !enabled) return;
        auto& counter = counters[static_cast<std::size_t>(stage_)];
        counter.seconds += Sys_FloatTime() - start_;
        ++counter.calls;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Stage stage_;
    std::uint64_t generation_;
    double start_{};
};
}
