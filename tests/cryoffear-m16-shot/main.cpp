// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/m16_shot.hpp"

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
    if (!value) { std::cerr << "M16 shot failed at " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
unsigned Bits(float value) { return std::bit_cast<unsigned>(value); }
float Float(unsigned value) { return std::bit_cast<float>(value); }
struct Context
{
    M16ShotState state;
    M16ShotFrame frame;
    float now{13.37F}, latchDeadline{}, nativeThink{};
    M16Vector punch{.1F, .2F, .3F};
    std::vector<unsigned> events;
    unsigned effects{8}, latch{}, calls{}, failAt{}, cancelAt{};
    int recoil{2};
    bool current{true}, record{true};
    bool Step() noexcept
    {
        ++calls;
        if (calls == cancelAt) current = false;
        return calls != failAt;
    }
    template<class... T> void Event(unsigned kind, T... values)
    {
        if (!record) return;
        events.insert(events.end(), {kind, sizeof...(T) + 8, static_cast<unsigned>(values)...,
            unsigned(state.control.action.clip), Bits(state.spread.scalar), Bits(state.spread.recoveryDeadline),
            Bits(state.control.action.idle), Bits(state.emptyDeadline), unsigned(state.control.action.burstShots),
            unsigned(state.burstPending), Bits(state.burstDeadline)});
    }
};
bool Current(void* p) noexcept { return static_cast<Context*>(p)->current; }
float Time(void* p) noexcept { return static_cast<Context*>(p)->now; }
bool Effect(void* p, const M16ShotEvent& e) noexcept
{
    auto& c = *static_cast<Context*>(p);
    const auto& a = e.first; const auto& b = e.second;
    switch (e.kind)
    {
    case M16ShotEffect::Shell:
        c.Event(5, Bits(a[0]), Bits(a[1]), Bits(a[2]), Bits(b[0]), Bits(b[1]), Bits(b[2]), Bits(e.a), e.value, e.other); break;
    case M16ShotEffect::Gunsmoke: CHECK(e.value == 19 && e.other == 32 && e.a == 1); c.Event(6); break;
    case M16ShotEffect::FireEvent:
        c.Event(7, Bits(a[0]), Bits(a[1]), Bits(a[2]), Bits(b[0]), Bits(b[1]), Bits(b[2]), e.value, e.other); break;
    case M16ShotEffect::PlayerFire: c.Event(34); break;
    case M16ShotEffect::Recoil:
        c.Event(8, Bits(a[0]), Bits(a[1]), Bits(a[2]));
        c.punch[0] += .25F; c.punch[1] -= .5F; c.punch[2] += .75F; break;
    case M16ShotEffect::Shake: c.Event(9, Bits(e.a), Bits(e.b), Bits(e.c), Bits(float(e.value))); break;
    case M16ShotEffect::ViewAnimation: c.Event(3, e.value); break;
    case M16ShotEffect::PlayerMove: c.Event(33, 5); break;
    case M16ShotEffect::MuzzleFlash: c.effects |= 2; break;
    case M16ShotEffect::ShotState: c.Event(12, Bits(e.a)); break;
    case M16ShotEffect::ShotLatch: c.latch = e.value; c.latchDeadline = e.a; break;
    case M16ShotEffect::EmptyClick: c.Event(4); break;
    case M16ShotEffect::ScheduleBurst: c.nativeThink = e.a; break;
    }
    return c.Step();
}
bool Reload(void* p) noexcept { auto& c = *static_cast<Context*>(p); c.Event(40); return c.Step(); }
bool MakeVectors(void* p, const M16Vector& v, M16Basis& out) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(30, Bits(v[0]), Bits(v[1]), Bits(v[2]));
    out = {{1,0,0}, {0,1,0}, {0,0,1}}; return c.Step();
}
bool GunPosition(void* p, M16Vector& out) noexcept { out = {2,3,4}; return static_cast<Context*>(p)->Step(); }
bool ShotAngles(void* p, M16Vector& out) noexcept
{
    auto& c = *static_cast<Context*>(p);
    constexpr M16Vector original{.1F,.2F,.3F};
    for (unsigned i = 0; i != 3; ++i) out[i] = c.frame.viewAngles[i] + (staleShotAngles ? original[i] : c.punch[i]);
    return c.Step();
}
bool AutoAim(void* p, float slope, M16Vector& out) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(35, Bits(slope)); out = {1,0,0}; return c.Step();
}
bool Random(void* p, int low, int high, int& out) noexcept
{
    auto& c = *static_cast<Context*>(p); CHECK(low == 0 && high == 2);
    c.Event(32, low, high); out = c.recoil; return c.Step();
}
bool Bullet(void* p, const M16Bullet& b) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(31, Bits(b.source[0]), Bits(b.source[1]), Bits(b.source[2]),
        Bits(b.direction[0]), Bits(b.direction[1]), Bits(b.direction[2]),
        Bits(b.spread[0]), Bits(b.spread[1]), Bits(b.spread[2]), Bits(8192.F), b.damage, b.seed);
    return c.Step();
}
bool SoundPosition(void* p, M16Vector& out) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(41); out = {12,14,16}; return c.Step();
}
bool Sound(void* p, const M16Vector& v) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(42, 0, 1, 1, Bits(v[0]), Bits(v[1]), Bits(v[2])); return c.Step();
}
M16ShotHost Host(Context& c) noexcept
{
    return {&c, Current, Time, Effect, Reload, MakeVectors, GunPosition, ShotAngles, AutoAim,
        Random, Bullet, SoundPosition, Sound};
}
Context Initial()
{
    Context c;
    c.state.control.action.clip = 75; c.state.spread.vector = {.01F,.01F,.01F};
    c.frame.damage = 37; c.frame.shellModel = 31; c.frame.weaponEntity = 19; c.frame.viewModel = 32;
    c.frame.weaponOrigin = c.frame.ownerOrigin = {11,12,13}; c.frame.weaponViewOffset = {1,2,3};
    c.frame.weaponVelocityY = 5; c.frame.viewAngles = {1,2,3};
    c.frame.initialBasis = {{1,2,3}, {7,8,9}, {4,5,6}}; c.frame.randomSeed = 17;
    c.events.reserve(512); return c;
}
std::vector<unsigned> Words(const std::string& text)
{
    std::istringstream in(text); std::vector<unsigned> result;
    unsigned v{}; while (in >> std::hex >> v) result.push_back(v);
    if (!in.eof()) return {};
    return result;
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
        const auto v = Words(input);
        if (v.size() != 21) return 3;
        if (!std::getline(expected, answer)) return 4;
        auto c = Initial(); auto& a = c.state.control.action; auto& f = c.frame;
        c.now = Float(v[0]); a.clip = int(v[1]); a.primary = Float(v[2]); a.action = int(v[3]);
        a.special = int(v[4]); f.waterLevel = int(v[5]); f.fireOnEmpty = v[6] != 0;
        a.aiming = v[7] != 0; f.infiniteAmmo = v[8] != 0; f.ownerFlags = v[9];
        c.state.spread.scalar = Float(v[10]); c.state.emptyDeadline = Float(v[11]);
        f.damage = Float(v[12]); c.recoil = int(v[13]); f.eventOwner2814 = int(v[14]); f.randomSeed = v[15];
        c.state.control.burst = v[16] != 0; a.burstShots = int(v[17]); c.state.spreadIncrement = Float(v[18]);
        c.state.burstDeadline = c.nativeThink = Float(v[19]); c.state.burstPending = v[20] != 0;
        const auto result = M16Shoot(c.state, f, Host(c));
        CHECK(result == M16ShotResult::Fired || result == M16ShotResult::Empty ||
            result == M16ShotResult::ReloadRequested || result == M16ShotResult::Ineligible);
        std::vector<unsigned> actual{unsigned(a.clip), Bits(a.primary), Bits(a.secondary), Bits(a.idle),
            Bits(c.state.emptyDeadline), Bits(c.state.spread.scalar), Bits(c.state.spread.recoveryDeadline),
            c.latch, Bits(c.latchDeadline), c.effects, unsigned(a.burstShots), unsigned(c.state.burstPending),
            Bits(c.state.burstDeadline), Bits(c.nativeThink)};
        actual.insert(actual.end(), c.events.begin(), c.events.end());
        const auto wanted = Words(answer);
        if (actual != wanted)
        {
            std::cerr << "case " << count << " words=" << actual.size() << '/' << wanted.size() << '\n';
            for (unsigned i = 0; i < std::min(actual.size(), wanted.size()); ++i)
                if (actual[i] != wanted[i]) { std::cerr << "word " << i << " actual=" << std::hex << actual[i] << " expected=" << wanted[i] << '\n'; break; }
            return 5;
        }
        checks += unsigned(wanted.size()); ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, answer)) return 7;
    auto baseline = Initial();
    CHECK(M16Shoot(baseline.state, baseline.frame, Host(baseline)) == M16ShotResult::Fired);
    const unsigned calls = baseline.calls;
    for (unsigned i = 1; i <= calls; ++i)
    {
        auto c = Initial(); c.cancelAt = i;
        CHECK(M16Shoot(c.state, c.frame, Host(c)) == M16ShotResult::Cancelled && c.calls == i);
        c = Initial(); c.failAt = i;
        CHECK(M16Shoot(c.state, c.frame, Host(c)) == M16ShotResult::ConsumerRejected && c.calls == i && c.current);
        if (i == calls) CHECK(!c.state.burstPending && c.state.burstDeadline == 0);
        c.failAt = 0; c.now += 2;
        CHECK(M16Shoot(c.state, c.frame, Host(c)) == M16ShotResult::Fired);
    }
    auto c = Initial(); auto host = Host(c); host.autoAim = nullptr;
    CHECK(M16Shoot(c.state, c.frame, host) == M16ShotResult::MissingHost && !c.calls);
    c = Initial(); c.frame.damage = std::numeric_limits<float>::infinity();
    CHECK(M16Shoot(c.state, c.frame, Host(c)) == M16ShotResult::InvalidInput && !c.calls);
    c = Initial(); c.current = false;
    CHECK(M16Shoot(c.state, c.frame, Host(c)) == M16ShotResult::Cancelled && !c.calls);
    c = Initial(); c.record = false; host = Host(c);
    const auto before = allocations;
    for (unsigned i = 0; i != 10000; ++i)
    {
        c.state = {}; c.state.control.action.clip = 75; c.now = float(i + 1);
        CHECK(M16Shoot(c.state, c.frame, host) == M16ShotResult::Fired);
    }
    CHECK(allocations == before);
    std::cout << "M16 shot cases=" << count << " checks=" << checks << " cancellationEdges=" << calls
        << " hotCalls=10000 allocations=0\n";
}
