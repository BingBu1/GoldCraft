// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/automatic_sound.hpp"
#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace guest::cof;
struct Context
{
    AutomaticSoundState* state{};
    bool current{true}, reject{};
    unsigned count{}, cancelAt{99};
    std::array<std::array<unsigned, 7>, 2> events{};
    static bool Current(void* p) noexcept { return static_cast<Context*>(p)->current; }
    static bool Event(void* p, const AutomaticSoundEvent& event) noexcept
    {
        auto& self = *static_cast<Context*>(p);
        if (self.count == self.events.size()) std::terminate();
        self.events[self.count++] = {unsigned(event.sample), unsigned(event.channel), unsigned(event.flags),
            unsigned(event.pitch), std::bit_cast<unsigned>(event.volume), std::bit_cast<unsigned>(event.attenuation),
            self.state->playing};
        if (self.count == self.cancelAt) self.current = false;
        return !self.reject;
    }
    AutomaticSoundHost Host() { return {this, Current, Event}; }
};

void Safety()
{
    unsigned checks{};
    const auto check = [&](bool value) { ++checks; if (!value) std::exit(3); };
    for (const bool start : {false, true})
    {
        AutomaticSoundState state{!start};
        Context context{&state};
        AutomaticSoundFrame frame{0, 0, false, true, false, true, false, true, true};
        const auto run = [&] { return start ? StartAutomaticNativeSound(state, frame, context.Host()) :
                                             AdvanceAutomaticNativeSound(state, frame, context.Host()); };
        frame.nativeMultiplayer = false;
        check(run() == AutomaticSoundResult::UnsupportedRules && state.playing == !start && !context.count);
        frame.nativeMultiplayer = true;
        frame.clip = -1;
        check(run() == AutomaticSoundResult::InvalidInput && state.playing == !start && !context.count);
        frame.clip = 0;
        context.current = false;
        check(run() == AutomaticSoundResult::Cancelled && state.playing == !start && !context.count);
        context.current = true;
        auto missing = context.Host(); missing.event = nullptr;
        check((start ? StartAutomaticNativeSound(state, frame, missing) :
                       AdvanceAutomaticNativeSound(state, frame, missing)) == AutomaticSoundResult::MissingHost);
        check(state.playing == !start && !context.count);
        context.cancelAt = 1;
        check(run() == AutomaticSoundResult::Cancelled && state.playing == start && context.count == 1);
        state.playing = !start;
        context = Context{&state}; context.reject = true;
        check(run() == AutomaticSoundResult::Cancelled && state.playing == start && context.count == 1);
    }
    AutomaticSoundState state;
    Context context{&state};
    AutomaticSoundFrame frame{75, 0, false, true, true, true, false, true, true};
    check(StartAutomaticNativeSound(state, frame, context.Host()) == AutomaticSoundResult::Started);
    for (unsigned i = 0; i < 100; ++i)
    {
        check(StartAutomaticNativeSound(state, frame, context.Host()) == AutomaticSoundResult::Unchanged);
        check(AdvanceAutomaticNativeSound(state, frame, context.Host()) == AutomaticSoundResult::Unchanged);
    }
    check(state.playing && context.count == 1);
    context.count = 0;
    frame.attackHeld = false;
    check(AdvanceAutomaticNativeSound(state, frame, context.Host()) == AutomaticSoundResult::Stopped);
    check(!state.playing && context.count == 2 && context.events[0][2] == 32 && context.events[1][0] == 1);
    std::cout << "safety=" << checks << '\n';
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--safety") { Safety(); return 0; }
    if (argc != 1) return 4;
    std::array<unsigned, 10> input{};
    while (std::cin >> input[0])
    {
        for (unsigned i = 1; i < input.size(); ++i) if (!(std::cin >> input[i])) return 5;
        if (input[0] > 2) return 6;
        AutomaticSoundState state{input[1] != 0};
        AutomaticSoundFrame frame{int(input[2]), int(input[3]), input[4] != 0, input[5] != 0,
            input[6] != 0, true, input[7] != 0, input[8] != 0, input[9] != 0};
        Context context{&state};
        const auto result = input[0] == 2 ? AdvanceAutomaticNativeSound(state, frame, context.Host()) :
                                            StartAutomaticNativeSound(state, frame, context.Host());
        if (result != AutomaticSoundResult::Unchanged && result != AutomaticSoundResult::Started &&
            result != AutomaticSoundResult::Stopped) return 7;
        std::cout << state.playing << ' ' << context.count;
        for (unsigned i = 0; i < context.count; ++i)
            for (const auto word : context.events[i]) std::cout << ' ' << word;
        std::cout << '\n';
    }
}
