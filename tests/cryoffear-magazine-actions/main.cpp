// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/magazine_actions.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <cstdlib>

using namespace guest::cof;
using Words = std::array<std::uint32_t, 14>;
Words Save(const MagazineActionState& s)
{
    return {std::uint32_t(s.clip), std::uint32_t(s.reserve), std::uint32_t(s.action),
        std::uint32_t(s.special), std::uint32_t(s.shotPhase), std::uint32_t(s.burstShots),
        s.aiming, s.pendingReload, s.loading, std::bit_cast<std::uint32_t>(s.primary),
        std::bit_cast<std::uint32_t>(s.secondary), std::bit_cast<std::uint32_t>(s.extra),
        std::bit_cast<std::uint32_t>(s.idle), std::bit_cast<std::uint32_t>(s.ownerAttack)};
}
struct Context
{
    MagazineActionState* state{};
    std::array<std::array<std::uint32_t, 17>, 3> events{};
    unsigned count{};
    unsigned cancelAt{99};
    bool alive{true};
    static bool Current(void* value) noexcept { return static_cast<Context*>(value)->alive; }
    static bool Event(void* value, const MagazineEvent& event) noexcept
    {
        auto& self = *static_cast<Context*>(value);
        if (self.count == self.events.size()) std::terminate();
        auto& row = self.events[self.count++];
        row[0] = std::uint32_t(event.kind); row[1] = event.argument;
        row[2] = std::bit_cast<std::uint32_t>(event.delay);
        const auto state = Save(*self.state);
        std::copy(state.begin(), state.end(), row.begin() + 3);
        if (self.count == self.cancelAt) self.alive = false;
        return true;
    }
    MagazineActionHost Host() { return {this, Current, Event}; }
};

void Safety()
{
    unsigned checks{};
    auto require = [&](bool condition) { ++checks; if (!condition) std::exit(3); };
    MagazineSequenceTimes times; times.present = (std::uint64_t{1} << 38) - 1;
    times.duration.fill(2.5f);
    MagazineActionState initial{4, 20, 0, 0, 4, 2, false, false, false, 0, 0, 0, 0, 0};
    for (unsigned operation = 0; operation != 3; ++operation)
    {
        auto invoke = [&](Pistol pistol, MagazineActionState& state, float now,
                          const MagazineSequenceTimes& data, MagazineActionHost host) {
            if (operation == 0) return StartMagazineReload(pistol, state, now, data, host);
            if (operation == 1) return StartPistolMelee(pistol, state, now, host);
            return MagazineIdle(pistol, state, now, data, host);
        };
        auto state = initial; Context context{&state}; const auto saved = Save(state);
        require(invoke(Pistol::Revolver, state, 10, times, context.Host()) == MagazineActionResult::UnsupportedWeapon);
        require(Save(state) == saved && context.count == 0);
        require(invoke(Pistol::P345, state, 10, times, {}) == MagazineActionResult::MissingHost);
        require(Save(state) == saved);
        context.alive = false;
        require(invoke(Pistol::P345, state, 10, times, context.Host()) == MagazineActionResult::Cancelled);
        require(Save(state) == saved);
        context.alive = true;
        require(invoke(Pistol::P345, state, std::numeric_limits<float>::quiet_NaN(), times,
            context.Host()) == MagazineActionResult::InvalidInput);
        require(Save(state) == saved);
        state.clip = -1;
        require(invoke(Pistol::P345, state, 10, times, context.Host()) == MagazineActionResult::InvalidInput);
        state = initial;
        if (operation != 1)
        {
            require(invoke(Pistol::P345, state, 10, {}, context.Host()) == MagazineActionResult::MissingSequence);
            require(Save(state) == saved && context.count == 0);
        }
        for (unsigned stop = 1; stop <= (operation == 1 ? 3u : operation == 0 ? 2u : 1u); ++stop)
        {
            state = initial; context = Context{&state}; context.cancelAt = stop;
            require(invoke(Pistol::P345, state, 10, times, context.Host()) == MagazineActionResult::Cancelled);
            require(context.count == stop);
            if (operation == 1) require(state.primary == 0 && state.secondary == 0 && state.idle == 0);
            if (operation == 2) require(state.idle == 0);
            if (operation == 0 && stop == 1) require(!state.loading);
        }
    }
    for(Pistol pistol:{Pistol::Glock,Pistol::P345,Pistol::VP70})
    {
        auto state=initial;state.clip=75;Context context{&state};const auto saved=Save(state);
        require(StartMagazineReload(pistol,state,10,times,context.Host())==MagazineActionResult::Ineligible);
        require(Save(state)==saved&&context.count==0);
        require(StartPistolMelee(pistol,state,10,context.Host())==MagazineActionResult::Applied);
        require(state.clip==75&&state.reserve==initial.reserve&&context.count==3);
        state=initial;state.clip=75;context=Context{&state};
        const auto idle=MagazineIdle(pistol,state,10,times,context.Host());
        require(idle==(pistol==Pistol::VP70?MagazineActionResult::Ineligible:MagazineActionResult::Applied));
        require(state.clip==75&&state.reserve==initial.reserve);
    }
    std::cout << "safety=" << checks << '\n';
}

int main(int argc, char**)
{
    if (argc != 1) { Safety(); return 0; }
    std::array<MagazineSequenceTimes, 4> times{};
    for (unsigned id : {0u, 1u, 3u})
    {
        for (unsigned i = 0; i < 38; ++i)
        {
            std::uint32_t word{};
            if (!(std::cin >> word)) return 4;
            times[id].duration[i] = std::bit_cast<float>(word);
        }
        times[id].present = (std::uint64_t{1} << 38) - 1;
    }
    std::array<std::uint32_t, 17> row{};
    while (std::cin >> row[0])
    {
        for (unsigned i = 1; i < row.size(); ++i) if (!(std::cin >> row[i])) return 5;
        if (row[1] > 3 || row[0] > 2) return 6;
        auto f = [&](unsigned i) { return std::bit_cast<float>(row[i]); };
        MagazineActionState state{int(row[2]), int(row[3]), int(row[4]), int(row[5]), int(row[6]), int(row[7]),
            row[8] != 0, row[9] != 0, row[10] != 0, f(11), f(12), f(13), f(14), f(15)};
        Context context{&state}; const auto host = context.Host();
        const auto pistol = static_cast<Pistol>(row[1]);
        MagazineActionResult result;
        if (row[0] == 0) result = StartMagazineReload(pistol, state, f(16), times[row[1]], host);
        else if (row[0] == 1) result = StartPistolMelee(pistol, state, f(16), host);
        else result = MagazineIdle(pistol, state, f(16), times[row[1]], host);
        if (result != MagazineActionResult::Applied && result != MagazineActionResult::Deferred &&
            result != MagazineActionResult::Ineligible) return 7;
        for (auto word : Save(state)) std::cout << word << ' ';
        std::cout << context.count;
        for (unsigned i = 0; i < context.count; ++i)
            for (auto word : context.events[i]) std::cout << ' ' << word;
        std::cout << '\n';
    }
}
