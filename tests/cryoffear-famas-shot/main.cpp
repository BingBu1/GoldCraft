// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/famas_shot.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace { std::size_t allocations{}; }
void* operator new(std::size_t size)
{
    ++allocations;
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace
{
using namespace guest::cof;
unsigned checks{};
bool staleShotAngles{};
void Check(bool value, int line)
{
    ++checks;
    if (!value) { std::cerr << "FAMAS shot failed at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
unsigned Bits(float value) { return std::bit_cast<unsigned>(value); }
float Float(unsigned value) { return std::bit_cast<float>(value); }
struct Context
{
    FamasShotState state;
    FamasShotFrame frame;
    float now{13.37F}, latchDeadline{};
    FamasVector punch{.1F, .2F, .3F};
    std::vector<unsigned> events;
    unsigned effects{8}, latch{}, calls{}, failAt{}, cancelAt{}, bullets{};
    int recoil{2}, animation{2}, damage{};
    bool current{true}, record{true}, failedOptional{};
    bool Step(bool optional = false) noexcept
    {
        ++calls;
        if (calls == cancelAt) current = false;
        if (calls == failAt) { failedOptional = optional; return false; }
        return true;
    }
    template<class... T> void Event(unsigned kind, T... values)
    {
        if (!record) return;
        events.insert(events.end(), {kind, sizeof...(T) + 5, static_cast<unsigned>(values)...,
            unsigned(state.action.clip), Bits(state.spread.scalar), Bits(state.spread.recoveryDeadline),
            Bits(state.action.idle), Bits(state.emptyDeadline)});
    }
};
bool Current(void* p) noexcept { return static_cast<Context*>(p)->current; }
float Time(void* p) noexcept { return static_cast<Context*>(p)->now; }
bool Effect(void* p, const FamasShotEvent& e) noexcept
{
    auto& c = *static_cast<Context*>(p);
    const auto& a = e.first; const auto& b = e.second;
    switch (e.kind)
    {
    case FamasShotEffect::Shell:
        c.Event(5, Bits(a[0]), Bits(a[1]), Bits(a[2]), Bits(b[0]), Bits(b[1]), Bits(b[2]), Bits(e.a), e.value, e.other); break;
    case FamasShotEffect::Gunsmoke: CHECK(e.value == 19 && e.other == 32 && e.a == 1); c.Event(6); break;
    case FamasShotEffect::FireEvent:
        c.Event(7, Bits(a[0]), Bits(a[1]), Bits(a[2]), Bits(b[0]), Bits(b[1]), Bits(b[2]), e.value, e.other); break;
    case FamasShotEffect::Shake: c.Event(9, Bits(e.a), Bits(e.b), Bits(e.c), Bits(float(e.value))); break;
    case FamasShotEffect::ViewAnimation: c.Event(3, e.value); break;
    case FamasShotEffect::PlayerMove: c.Event(33, 5); break;
    case FamasShotEffect::MuzzleFlash: c.effects |= 2; break;
    case FamasShotEffect::ShotState: c.Event(12, Bits(e.a)); break;
    case FamasShotEffect::ShotLatch: c.latch = e.value; c.latchDeadline = e.a; break;
    case FamasShotEffect::EmptyClick: c.Event(4); break;
    }
    return c.Step(true);
}
bool Reload(void* p) noexcept { auto& c = *static_cast<Context*>(p); c.Event(40); return c.Step(); }
bool MakeVectors(void* p, const FamasVector& v, FamasBasis& out) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(30, Bits(v[0]), Bits(v[1]), Bits(v[2]));
    out = {{1,0,0}, {0,1,0}, {0,0,1}}; return c.Step();
}
bool GunPosition(void* p, FamasVector& out) noexcept { out = {2,3,4}; return static_cast<Context*>(p)->Step(); }
bool Recoil(void* p, const FamasVector& v) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(8, Bits(v[0]), Bits(v[1]), Bits(v[2]));
    c.punch[0] += .25F; c.punch[1] -= .5F; c.punch[2] += .75F;
    return c.Step();
}
bool ShotAngles(void* p, FamasVector& out) noexcept
{
    auto& c = *static_cast<Context*>(p);
    constexpr FamasVector original{.1F,.2F,.3F};
    for (unsigned i = 0; i != 3; ++i) out[i] = c.frame.viewAngles[i] + (staleShotAngles ? original[i] : c.punch[i]);
    return c.Step();
}
bool AutoAim(void* p, float slope, FamasVector& out) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(35, Bits(slope)); out = {1,0,0}; return c.Step();
}
bool Random(void* p, int low, int high, int& out) noexcept
{
    auto& c = *static_cast<Context*>(p); CHECK(low == -2 && high == 2);
    c.Event(32, low, high); out = c.recoil; return c.Step();
}
bool Bullet(void* p, const FamasBullet& b) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(31, Bits(b.source[0]), Bits(b.source[1]), Bits(b.source[2]),
        Bits(b.direction[0]), Bits(b.direction[1]), Bits(b.direction[2]),
        Bits(b.spread[0]), Bits(b.spread[1]), Bits(b.spread[2]), Bits(8192.F), b.damage, b.seed);
    ++c.bullets; c.damage = b.damage;
    return c.Step();
}
bool SoundPosition(void* p, FamasVector& out) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(41); out = {12,14,16}; return c.Step(true);
}
bool Sound(void* p, const FamasVector& v) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(42, 0, 1, 1, Bits(v[0]), Bits(v[1]), Bits(v[2])); return c.Step(true);
}
FamasShotHost Host(Context& c) noexcept
{
    return {&c, Current, Time, Effect, Reload, MakeVectors, GunPosition, Recoil, ShotAngles,
        AutoAim, Random, Bullet, SoundPosition, Sound};
}
Context Initial()
{
    Context c;
    c.state.action.clip = 75; c.state.spread.vector = {.01F,.01F,.01F}; c.state.spreadIncrement = .75F;
    c.frame.damage = 37; c.frame.shellModel = 31; c.frame.weaponEntity = 19; c.frame.viewModel = 32;
    c.frame.weaponOrigin = c.frame.ownerOrigin = {11,12,13}; c.frame.weaponViewOffset = {1,2,3};
    c.frame.weaponVelocityY = 5; c.frame.viewAngles = {1,2,3};
    c.frame.initialBasis = {{1,2,3}, {7,8,9}, {4,5,6}}; c.frame.randomSeed = 17;
    c.events.reserve(512); return c;
}
std::vector<unsigned> Words(const std::string& text)
{
    std::istringstream in(text); std::vector<unsigned> result;
    unsigned value{}; while (in >> std::hex >> value) result.push_back(value);
    if (!in.eof()) return {};
    return result;
}
void Safety()
{
    auto c = Initial();
    CHECK(FamasPrimaryAttack(c.state, c.frame, {}) == FamasShotResult::MissingHost);
    CHECK(c.calls == 0 && c.state.action.clip == 75);
    CHECK(FamasPrimaryAttack(c.state, c.frame, Host(c)) == FamasShotResult::Fired);
    CHECK(c.bullets == 1 && c.damage == 37);
    // The original FAMAS has no named player-fire or animation-variation RNG call.
    const unsigned count = c.calls; CHECK(count == 20);
    for (unsigned i = 1; i <= count; ++i)
    {
        c = Initial(); c.cancelAt = i;
        CHECK(FamasPrimaryAttack(c.state, c.frame, Host(c)) == FamasShotResult::Cancelled);
        CHECK(c.calls == i && c.state.action.primary == 0);
        c = Initial(); c.failAt = i;
        const auto result = FamasPrimaryAttack(c.state, c.frame, Host(c));
        if (c.failedOptional)
        {
            CHECK(result == FamasShotResult::FiredWithoutPresentation && c.bullets == 1);
            CHECK(c.state.action.clip == 74 && c.state.action.primary > c.now);
            CHECK(c.state.action.secondary == c.now + 1.F);
        }
        else CHECK(result == FamasShotResult::ConsumerRejected && c.calls == i && c.state.action.primary == 0);
    }
    c = Initial(); auto host = Host(c); host.reload = nullptr;
    CHECK(FamasPrimaryAttack(c.state, c.frame, host) == FamasShotResult::Fired && c.bullets == 1);
    c = Initial(); host = Host(c); host.effect = nullptr; host.soundPosition = nullptr; host.sound = nullptr;
    CHECK(FamasPrimaryAttack(c.state, c.frame, host) == FamasShotResult::FiredWithoutPresentation);
    CHECK(c.bullets == 1 && c.state.action.clip == 74 && c.state.action.primary > c.now);
    c = Initial(); c.frame.weaponOrigin[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(FamasPrimaryAttack(c.state, c.frame, Host(c)) == FamasShotResult::FiredWithoutPresentation && c.bullets == 1);
    c = Initial(); c.state.action.clip = 0; c.frame.fireOnEmpty = true; host = Host(c); host.effect = nullptr;
    CHECK(FamasPrimaryAttack(c.state, c.frame, host) == FamasShotResult::EmptyWithoutPresentation);
    CHECK(c.state.action.primary > c.now && c.state.action.secondary == c.state.action.primary);
    c = Initial(); c.frame.damage = std::numeric_limits<float>::quiet_NaN();
    CHECK(FamasPrimaryAttack(c.state, c.frame, Host(c)) == FamasShotResult::InvalidInput && !c.calls);
    c = Initial(); c.current = false;
    CHECK(FamasPrimaryAttack(c.state, c.frame, Host(c)) == FamasShotResult::Cancelled && !c.calls);
    c = Initial(); c.record = false;
    const auto state = c.state; const auto before = allocations;
    for (unsigned i = 0; i < 10000; ++i)
    {
        c.state = state; c.punch = {.1F,.2F,.3F}; c.calls = 0;
        CHECK(FamasPrimaryAttack(c.state, c.frame, Host(c)) == FamasShotResult::Fired);
    }
    CHECK(allocations == before);
}
}

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) return 1;
    if (argc == 4)
    {
        if (std::string_view(argv[3]) != "--stale-angles") return 1;
        staleShotAngles = true;
    }
    std::ifstream cases(argv[1]), expected(argv[2]);
    if (!cases || !expected) return 1;
    std::string input, answer; unsigned count{};
    while (std::getline(cases, input))
    {
        const auto v = Words(input); if (v.size() != 20) return 3;
        if (!std::getline(expected, answer)) return 4;
        auto c = Initial(); auto& a = c.state.action; auto& f = c.frame;
        c.now = Float(v[0]); a.clip = int(v[1]); a.primary = Float(v[2]); a.action = int(v[3]);
        a.special = int(v[4]); f.waterLevel = int(v[5]); f.fireOnEmpty = v[6] != 0;
        a.aiming = v[7] != 0; f.infiniteAmmo = v[8] != 0; f.ownerFlags = v[9];
        c.state.spread.scalar = Float(v[10]); c.state.emptyDeadline = Float(v[11]);
        f.damage = Float(v[12]); c.recoil = int(v[13]); c.animation = int(v[14]);
        f.eventOwner2814 = int(v[15]); f.randomSeed = v[16]; c.state.spreadIncrement = Float(v[17]);
        // v18 seeds the original caller's button state; FAMAS primary has no extra button gate.
        a.secondary = Float(v[19]);
        const auto result = FamasPrimaryAttack(c.state, f, Host(c));
        CHECK(result == FamasShotResult::Fired || result == FamasShotResult::Empty ||
            result == FamasShotResult::ReloadRequested || result == FamasShotResult::Ineligible);
        std::vector<unsigned> actual{unsigned(a.clip), Bits(a.primary), Bits(a.secondary), Bits(a.idle),
            Bits(c.state.emptyDeadline), Bits(c.state.spread.scalar), Bits(c.state.spread.recoveryDeadline),
            c.latch, Bits(c.latchDeadline), c.effects};
        actual.insert(actual.end(), c.events.begin(), c.events.end());
        const auto wanted = Words(answer);
        if (actual != wanted)
        {
            std::cerr << "Original FAMAS mismatch at case " << count << '\n';
            for (unsigned i = 0; i < std::min(actual.size(), wanted.size()); ++i)
                if (actual[i] != wanted[i]) std::cerr << i << ":" << std::hex << actual[i] << " != " << wanted[i] << '\n';
            return 5;
        }
        checks += unsigned(actual.size()); ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, answer)) return 7;
    Safety();
    std::cout << "originalCases=" << count << " checks=" << checks << " failures=0\n";
    return 0;
}
