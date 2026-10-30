// SPDX-License-Identifier: GPL-3.0-or-later
#include "csol/scenario_music.hpp"
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>

namespace {
int checks{};
void Check(bool ok, const std::source_location where=std::source_location::current())
{
    ++checks;
    if (!ok) throw std::runtime_error("countdown BGM line " + std::to_string(where.line()));
}
}

int main()
{
    using csol::music::Countdown;
    using csol::music::Cue;
    try {
        Countdown clock;
        clock.Restart(100.0f);
        Check(clock.Tick(100.0f)==Cue::Ready);
        Check(clock.Tick(100.0f)==Cue::None);
        Check(clock.Tick(118.99f)==Cue::None);
        Check(clock.Tick(119.0f)==Cue::Round);
        Check(clock.Tick(120.0f)==Cue::None);
        Check(clock.Tick(1000.0f)==Cue::None);

        // Freeze expiry rebases the music clock without replaying Ready.
        clock.Restart(200.0f);
        Check(clock.Tick(200.0f)==Cue::Ready);
        clock.Thaw(205.0f);
        Check(clock.Tick(205.0f)==Cue::None);
        Check(clock.Tick(219.0f)==Cue::None);
        Check(clock.Tick(223.99f)==Cue::None);
        Check(clock.Tick(224.0f)==Cue::Round);
        Check(clock.Tick(224.0f)==Cue::None);
        clock.Thaw(300.0f);
        Check(clock.Tick(319.0f)==Cue::None);

        // A late first tick switches directly to round music.
        clock.Restart(400.0f);
        Check(clock.Tick(419.0f)==Cue::Round);
        Check(clock.Tick(420.0f)==Cue::None);
        clock.Restart(500.0f);
        Check(clock.Tick(600.0f)==Cue::Round);
        Check(clock.Tick(601.0f)==Cue::None);

        // A new stage resets both cues, including after a completed stage.
        clock.Restart(700.0f);
        Check(clock.Tick(700.0f)==Cue::Ready);
        Check(clock.Tick(718.0f)==Cue::None);
        Check(clock.Tick(719.0f)==Cue::Round);
        clock.Restart(800.0f);
        clock.Thaw(803.0f);
        Check(clock.Tick(803.0f)==Cue::Ready);
        Check(clock.Tick(821.99f)==Cue::None);
        Check(clock.Tick(822.0f)==Cue::Round);

        // Invalid times must not consume a cue or poison the next stage.
        clock.Restart(900.0f);
        Check(clock.Tick(std::numeric_limits<float>::quiet_NaN())==Cue::None);
        Check(clock.Tick(std::numeric_limits<float>::infinity())==Cue::None);
        Check(clock.Tick(900.0f)==Cue::Ready);
        clock.Restart(std::numeric_limits<float>::quiet_NaN());
        Check(clock.Tick(1000.0f)==Cue::None);
        clock.Restart(1000.0f);
        Check(clock.Tick(1000.0f)==Cue::Ready);
        Check(clock.Tick(1019.0f)==Cue::Round);
        Check(clock.Tick(1020.0f)==Cue::None);
        std::cout << "PASS BGM actual countdown " << checks << " checks; explicit clock inputs\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
