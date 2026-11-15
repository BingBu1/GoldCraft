// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/mp5_primary.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <vector>

namespace { std::size_t allocations{}; }
void* operator new(std::size_t size)
{
    ++allocations;
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

namespace
{
using namespace guest::cof;
unsigned checks{};
void Check(bool value, int line)
{
    ++checks;
    if (!value) { std::cerr << "MP5 check failed line " << line << '\n'; std::exit(2); }
}
#define CHECK(value) Check((value), __LINE__)
unsigned Bits(float value) { return std::bit_cast<unsigned>(value); }
float Float(unsigned value) { return std::bit_cast<float>(value); }
struct Context
{
    MP5PrimaryState state{};
    MP5PrimaryFrame frame{};
    std::vector<unsigned> events;
    unsigned effects{8}, latch{}, calls{}, failAt{}, cancelAt{};
    float latchDeadline{};
    int recoil{2}, animation{2};
    bool playing{}, current{true}, record{true};
    bool Step() noexcept
    {
        ++calls;
        if (calls == cancelAt) current = false;
        return calls != failAt;
    }
    template<class... T> void Event(unsigned kind, T... values)
    {
        if (!record) return;
        events.insert(events.end(), {kind, sizeof...(T) + 5, static_cast<unsigned>(values)...,
            unsigned(state.action.clip), Bits(state.spread.scalar), Bits(state.spread.recoveryDeadline),
            Bits(state.action.idle), Bits(state.emptyDeadline)});
    }
};
bool Current(void* value) noexcept { return static_cast<Context*>(value)->current; }
bool Effect(void* value, const MP5PrimaryEvent& e) noexcept
{
    auto& c = *static_cast<Context*>(value);
    const auto& a = e.first; const auto& b = e.second;
    switch (e.kind)
    {
    case MP5PrimaryEffect::Shell:
        c.Event(5, Bits(a[0]), Bits(a[1]), Bits(a[2]), Bits(b[0]), Bits(b[1]), Bits(b[2]), Bits(e.a), e.value, e.other); break;
    case MP5PrimaryEffect::KillLocalParticles: c.Event(19, e.value); break;
    case MP5PrimaryEffect::Gunsmoke:
        CHECK(e.value == 19 && e.other == 32 && e.a == 1); c.Event(6); break;
    case MP5PrimaryEffect::FireEvent:
        c.Event(7, Bits(a[0]), Bits(a[1]), Bits(a[2]), Bits(b[0]), Bits(b[1]), Bits(b[2]), e.value, e.other); break;
    case MP5PrimaryEffect::PlayerFire: c.Event(34); break;
    case MP5PrimaryEffect::Recoil: c.Event(8, Bits(a[0]), Bits(a[1]), Bits(a[2])); break;
    case MP5PrimaryEffect::Shake: c.Event(9, Bits(e.a), Bits(e.b), Bits(e.c), Bits(float(e.value))); break;
    case MP5PrimaryEffect::ViewAnimation: c.Event(3, e.value); break;
    case MP5PrimaryEffect::PlayerMove: c.Event(33, 5); break;
    case MP5PrimaryEffect::MuzzleFlash: c.effects |= 2; break;
    case MP5PrimaryEffect::ShotState: c.Event(12, Bits(e.a)); break;
    case MP5PrimaryEffect::ShotLatch: c.latch = e.value; c.latchDeadline = e.a; break;
    case MP5PrimaryEffect::EmptyClick: c.Event(4); break;
    }
    return c.Step();
}
bool Reload(void* value) noexcept
{
    auto& c = *static_cast<Context*>(value); c.Event(40); return c.Step();
}
bool MakeVectors(void* value, const MP5Vector& angles, MP5Basis& out) noexcept
{
    auto& c = *static_cast<Context*>(value); c.Event(30, Bits(angles[0]), Bits(angles[1]), Bits(angles[2]));
    out = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; return c.Step();
}
bool GunPosition(void* value, MP5Vector& out) noexcept
{
    out = {2, 3, 4}; return static_cast<Context*>(value)->Step();
}
bool AutoAim(void* value, float slope, MP5Vector& out) noexcept
{
    auto& c = *static_cast<Context*>(value); c.Event(35, Bits(slope));
    out = {1.F, 0.F, 0.F}; return c.Step();
}
bool RandomLong(void* value, int low, int high, int& out) noexcept
{
    auto& c = *static_cast<Context*>(value); c.Event(32, low, high);
    CHECK((low == -2 && high == 2) || (low == 0 && high == 2));
    out = low == -2 ? c.recoil : c.animation; return c.Step();
}
bool Bullet(void* value, const MP5PrimaryBullet& b) noexcept
{
    auto& c = *static_cast<Context*>(value);
    c.Event(31, Bits(b.source[0]), Bits(b.source[1]), Bits(b.source[2]),
        Bits(b.direction[0]), Bits(b.direction[1]), Bits(b.direction[2]),
        Bits(b.spread[0]), Bits(b.spread[1]), Bits(b.spread[2]), Bits(8192.F), b.damage, b.seed);
    return c.Step();
}
bool Sound(void* value) noexcept
{
    auto& c = *static_cast<Context*>(value);
    if (!c.playing) { c.playing = true; c.Event(20, 0, 1, 0, 100, Bits(1.F), Bits(.8F)); }
    return c.Step();
}
MP5PrimaryHost Host(Context& c) noexcept
{
    return {&c, Current, Effect, Reload, MakeVectors, GunPosition, AutoAim, RandomLong, Bullet, Sound};
}
Context Initial()
{
    Context c;
    c.state.action.clip = 75; c.state.spread.vector = {.01F, .01F, .01F};
    c.frame.now = 13.37F; c.frame.damage = 37; c.frame.shellModel = 31;
    c.frame.weaponEntity = 19; c.frame.viewModel = 32;
    c.frame.weaponOrigin = c.frame.ownerOrigin = {11, 12, 13};
    c.frame.weaponViewOffset = {1, 2, 3}; c.frame.weaponVelocityY = 5;
    c.frame.viewAngles = {1, 2, 3}; c.frame.punchAngles = {.1F, .2F, .3F};
    c.frame.initialBasis = {{1, 2, 3}, {7, 8, 9}, {4, 5, 6}}; c.frame.randomSeed = 17;
    c.events.reserve(512); return c;
}
std::vector<unsigned> Words(const std::string& line)
{
    std::istringstream input(line); std::vector<unsigned> result;
    unsigned value{}; while (input >> std::hex >> value) result.push_back(value);
    if (!input.eof()) return {};
    return result;
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    std::ifstream cases(argv[1]), expected(argv[2]);
    if (!cases || !expected) return 1;
    std::string row, answer; unsigned count{};
    while (std::getline(cases, row))
    {
        const auto v = Words(row);
        if (v.size() != 18) return 3;
        if (!std::getline(expected, answer)) return 4;
        auto c = Initial();
        auto& a = c.state.action; auto& f = c.frame;
        f.now = Float(v[0]); a.clip = int(v[1]); a.primary = Float(v[2]); a.action = int(v[3]);
        a.special = int(v[4]); f.waterLevel = int(v[5]); f.fireOnEmpty = v[6] != 0;
        a.aiming = v[7] != 0; f.infiniteAmmo = v[8] != 0; f.ownerFlags = v[9];
        c.state.spread.scalar = Float(v[10]); c.state.emptyDeadline = Float(v[11]);
        f.damage = Float(v[12]); c.recoil = int(v[13]); c.animation = int(v[14]);
        f.eventOwner2814 = int(v[15]); f.randomSeed = v[16]; c.playing = v[17] != 0;
        const auto result = MP5PrimaryAttack(c.state, f, Host(c));
        CHECK(result == MP5PrimaryResult::Fired || result == MP5PrimaryResult::Empty ||
              result == MP5PrimaryResult::ReloadRequested || result == MP5PrimaryResult::Ineligible);
        std::vector<unsigned> actual{unsigned(a.clip), Bits(a.primary), Bits(a.secondary), Bits(a.idle),
            Bits(c.state.emptyDeadline), Bits(c.state.spread.scalar), Bits(c.state.spread.recoveryDeadline),
            c.latch, Bits(c.latchDeadline), c.effects, unsigned(c.playing)};
        actual.insert(actual.end(), c.events.begin(), c.events.end());
        const auto wanted = Words(answer);
        if (actual != wanted)
        {
            std::cerr << "Original differential mismatch case " << count << " actual/wanted words " << actual.size() << '/' << wanted.size() << '\n';
            for (unsigned i = 0; i < std::min(actual.size(), wanted.size()); ++i)
                if (actual[i] != wanted[i]) { std::cerr << "word " << i << " actual " << std::hex << actual[i] << " wanted " << wanted[i] << '\n'; break; }
            return 5;
        }
        checks += unsigned(wanted.size()); ++count;
    }
    if (!count) return 6;
    if (std::getline(expected, answer)) return 7;
    auto baseline = Initial();
    CHECK(MP5PrimaryAttack(baseline.state, baseline.frame, Host(baseline)) == MP5PrimaryResult::Fired);
    const unsigned calls = baseline.calls;
    for (unsigned i = 1; i <= calls; ++i)
    {
        auto c = Initial(); c.cancelAt = i;
        CHECK(MP5PrimaryAttack(c.state, c.frame, Host(c)) == MP5PrimaryResult::Cancelled);
        CHECK(c.calls == i);
        c = Initial(); c.failAt = i;
        CHECK(MP5PrimaryAttack(c.state, c.frame, Host(c)) == MP5PrimaryResult::ConsumerRejected);
        CHECK(c.calls == i && c.current);
        c.failAt = 0; c.frame.now += 2;
        CHECK(MP5PrimaryAttack(c.state, c.frame, Host(c)) == MP5PrimaryResult::Fired);
    }
    auto c = Initial(); auto host = Host(c); host.autoAim = nullptr;
    CHECK(MP5PrimaryAttack(c.state, c.frame, host) == MP5PrimaryResult::MissingHost && c.calls == 0);
    c = Initial(); c.frame.damage = std::numeric_limits<float>::infinity();
    CHECK(MP5PrimaryAttack(c.state, c.frame, Host(c)) == MP5PrimaryResult::InvalidInput && c.calls == 0);
    c = Initial(); c.current = false;
    CHECK(MP5PrimaryAttack(c.state, c.frame, Host(c)) == MP5PrimaryResult::Cancelled && c.calls == 0);
    c = Initial(); c.record = false; host = Host(c);
    const auto before = allocations;
    for (unsigned i = 0; i != 10000; ++i)
    {
        c.state = {}; c.state.action.clip = 75; c.frame.now = float(i + 1);
        CHECK(MP5PrimaryAttack(c.state, c.frame, host) == MP5PrimaryResult::Fired);
    }
    const auto hotAllocations = allocations - before;
    CHECK(hotAllocations == 0);
    std::cout << "MP5 primary cases=" << count << " checks=" << checks << " cancellationEdges=" << calls
              << " hotCalls=10000 allocations=" << hotAllocations << '\n';
}
