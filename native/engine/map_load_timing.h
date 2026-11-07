// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>

// Opt-in diagnostics for synchronous map loading; never changes the deadlines.
// Nested stages are reported separately and must not be added to their parent.
class GoldCraftMapLoadTiming
{
public:
    explicit GoldCraftMapLoadTiming(const char* stage) : stage_(stage)
    {
        const auto* setting = std::getenv("GOLDCRAFT_MAP_LOAD_TIMING");
        enabled_ = setting && setting[0] == '1' && setting[1] == '\0';
        if (enabled_) start_ = Sys_FloatTime();
    }

    ~GoldCraftMapLoadTiming()
    {
        if (enabled_)
            Log_Printf("GoldCraft mapload stage=%s map=%s seconds=%.6f\n",
                stage_, g_psv.name, Sys_FloatTime() - start_);
    }

    GoldCraftMapLoadTiming(const GoldCraftMapLoadTiming&) = delete;
    GoldCraftMapLoadTiming& operator=(const GoldCraftMapLoadTiming&) = delete;

private:
    const char* stage_;
    double start_{};
    bool enabled_{};
};
