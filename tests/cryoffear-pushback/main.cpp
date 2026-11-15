// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/guest/cof/pushback.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>

using namespace guest::cof;
namespace
{
std::uint64_t allocations{}, checks{};
bool staleStamina{};
void Check(bool ok)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "pushback check %llu failed\n", checks); std::exit(1); }
}
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float Number(std::uint32_t value) { return std::bit_cast<float>(value); }
struct Context
{
    float stamina{}, entryStamina{};
    bool infinite{}, current{true};
    unsigned count{}, mask{}, dead{}, mutation{}, newInfinite{}, damageResult{};
    float newStamina{};
    unsigned steps{}, cancelAt{}, rejectAt{}, size{};
    std::array<std::uint32_t, 512> events{};
    bool Step()
    {
        ++steps;
        if (steps == cancelAt) current = false;
        return steps != rejectAt;
    }
    template<class... Values> void Event(unsigned kind, Values... values)
    {
        Check(size + sizeof...(values) + 3 <= events.size());
        events[size++] = kind; events[size++] = sizeof...(values) + 1;
        ((events[size++] = static_cast<std::uint32_t>(values)), ...);
        events[size++] = Bits(stamina);
    }
    void Mutate(unsigned stage)
    {
        if (stage == mutation) { stamina = newStamina; infinite = newInfinite != 0; }
    }
    void Initialize(const std::array<std::uint32_t, 9>& row)
    {
        *this = {};
        stamina = entryStamina = Number(row[0]); infinite = row[1] != 0; count = row[2]; mask = row[3];
        dead = row[4]; mutation = row[5]; newStamina = Number(row[6]);
        newInfinite = row[7]; damageResult = row[8];
    }
};
bool Current(void* p) noexcept { return static_cast<Context*>(p)->current; }
bool Stamina(void* p, float& value) noexcept
{ auto& c = *static_cast<Context*>(p); value = staleStamina ? c.entryStamina : c.stamina; return c.Step(); }
bool Infinite(void* p, bool& value) noexcept
{ auto& c = *static_cast<Context*>(p); value = c.infinite; return c.Step(); }
bool SetStamina(void* p, float value) noexcept
{ auto& c = *static_cast<Context*>(p); c.stamina = value; return c.Step(); }
bool Sound(void* p, const PushbackSound& value) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(1, value.emitter, unsigned(value.sample), value.channel, Bits(value.volume),
            Bits(value.attenuation), value.flags, value.pitch);
    if (value.sample == PushbackSample::Swing) c.Mutate(1);
    return c.Step();
}
bool MakeVectors(void* p) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(2, Bits(4), Bits(5), Bits(6));
    c.Mutate(2); return c.Step();
}
bool Position(void* p, PushbackVector& value) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(3); value = {2, 3, 4};
    c.Mutate(3); return c.Step();
}
bool Recoil(void* p, const PushbackVector& value) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(4, Bits(value[0]), Bits(value[1]), Bits(value[2]));
    return c.Step();
}
bool Next(void* p, PushbackTarget previous, const PushbackVector& source, float radius,
          PushbackTarget& target) noexcept
{
    auto& c = *static_cast<Context*>(p);
    c.Event(5, previous, Bits(source[0]), Bits(source[1]), Bits(source[2]), Bits(radius));
    target = previous < c.count ? previous + 1 : 0; return c.Step();
}
bool Monster(void* p, PushbackTarget target, PushbackTarget& monster) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(6, target);
    monster = c.mask & (1U << (target - 1)) ? target : 0; return c.Step();
}
bool Alive(void* p, PushbackTarget target, bool& value) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(7, target);
    value = ((c.dead >> (2 * (target - 1))) & 3U) != 2; return c.Step();
}
bool Damage(void* p, PushbackTarget target, const PushbackDamage& value) noexcept
{
    auto& c = *static_cast<Context*>(p); c.Event(8, target, Bits(value.amount), value.originalBits);
    // Both target TakeDamage return values are delivered. They do not decide
    // whether the original callback emits its subsequent hit sound.
    return c.Step();
}
PushbackHost Host(Context& c)
{ return {&c, 2, Current, Stamina, Infinite, SetStamina, Sound, MakeVectors, Position,
          Recoil, Next, Monster, Alive, Damage}; }
}

void* operator new(std::size_t size)
{
    ++allocations;
    if (auto* value = std::malloc(size)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) return 1;
    staleStamina = argc == 4 && !std::strcmp(argv[3], "--stale-stamina");
    if (argc == 4 && !staleStamina) return 1;
    std::ifstream input(argv[1]), expected(argv[2]);
    if (!input || !expected) return 1;
    std::array<std::uint32_t, 9> row{};
    unsigned cases{};
    while (input >> std::hex >> row[0])
    {
        for (unsigned i = 1; i < row.size(); ++i) if (!(input >> std::hex >> row[i])) return 3;
        Context c; c.Initialize(row); const auto before = allocations;
        const auto result = LongGunPushback(Host(c));
        Check(allocations == before);
        Check(result == (Number(row[0]) > 0 ? PushbackResult::Applied : PushbackResult::Ineligible));
        std::uint32_t want{};
        if (!(expected >> std::hex >> want) || want != Bits(c.stamina)) return 5;
        for (unsigned i = 0; i < c.size; ++i)
        {
            if (!(expected >> std::hex >> want) || want != c.events[i]) return 5;
            ++checks;
        }
        ++cases;
    }
    if (!cases) return 6;
    std::uint32_t extra{};
    if (expected >> std::hex >> extra) return 7;
    const std::array<std::uint32_t, 9> good{Bits(100), 0, 6, 63, 0, 0, Bits(100), 0, 0};
    Context reference; reference.Initialize(good);
    Check(LongGunPushback(Host(reference)) == PushbackResult::Applied);
    const auto edges = reference.steps;
    for (unsigned edge = 1; edge <= edges; ++edge)
        for (bool cancel : {false, true})
        {
            Context c; c.Initialize(good);
            if (cancel) c.cancelAt = edge; else c.rejectAt = edge;
            Check(LongGunPushback(Host(c)) ==
                  (cancel ? PushbackResult::Cancelled : PushbackResult::MissingBackend));
            Check(c.steps == edge);
            c.current = true; c.cancelAt = c.rejectAt = c.steps = c.size = 0;
            Check(LongGunPushback(Host(c)) == PushbackResult::Applied);
        }
    for (float invalid : {std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()})
    {
        Context c; c.Initialize(good); c.stamina = invalid;
        Check(LongGunPushback(Host(c)) == PushbackResult::InvalidInput && !c.size);
    }
    Context missing; missing.Initialize(good); auto host = Host(missing); host.damage = nullptr;
    Check(LongGunPushback(host) == PushbackResult::MissingBackend && !missing.steps);
    for (unsigned i = 0; i < 10000; ++i)
    {
        Context c; c.Initialize(good); const auto before = allocations;
        Check(LongGunPushback(Host(c)) == PushbackResult::Applied && allocations == before);
    }
    std::printf("Pushback original cases=%u checks=%llu cancellationEdges=%u hotCalls=10000 allocations=0\n",
                cases, checks, edges);
}
