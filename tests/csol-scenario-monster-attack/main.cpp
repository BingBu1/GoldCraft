// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../../native/csol/server/scenario_monster_attack.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
using namespace csol::scenario;
int checks{}, failures{};
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float Float(std::uint32_t value) { return std::bit_cast<float>(value); }
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
std::array<std::uint32_t, 9> ClockBits(const CSOLMonsterMeleeClocks& clocks)
{
    return {Bits(clocks.attack.interval), Bits(clocks.attack.deadline), Bits(clocks.attack.stamp),
        Bits(clocks.avoidance.interval), Bits(clocks.avoidance.deadline), Bits(clocks.avoidance.stamp),
        Bits(clocks.idleObstacleCheck.interval), Bits(clocks.idleObstacleCheck.deadline), Bits(clocks.idleObstacleCheck.stamp)};
}
CCSOLMonsterAnimation* activeAnimation{};
CSOLMonsterMeleeClocks* activeClocks{};
void* activeModel{};
struct ResetCall
{
    int sequence{}, cache{};
    std::uint32_t frame{}, rate{}, lock{};
    std::array<std::uint32_t, 9> clocks{};
    bool operator==(const ResetCall&) const = default;
};
std::vector<ResetCall> resetCalls;
ResetCall State(CBaseAnimating& monster)
{
    return {monster.pev->sequence, activeAnimation->CachedSequence(), Bits(monster.pev->frame),
        Bits(monster.pev->framerate), Bits(activeAnimation->LockedUntil()), ClockBits(*activeClocks)};
}
void* ModelPointer(edict_t*) { return activeModel; }
struct Fixture
{
    globalvars_t globals{};
    edict_t edict{};
    CBaseAnimating monster{};
    CCSOLMonsterAnimation animation;
    MonsterSequences sequences;
    CSOLMonsterMeleeClocks clocks{{11, 12, 13}, {21, 22, 23}, {31, 32, 33}};
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t, 128> sequence{}; } model;
    Fixture(bool withModel = true)
    {
        gpGlobals = &globals; globals.time = 25;
        g_engfuncs = {}; g_engfuncs.pfnGetModelPtr = ModelPointer;
        monster.pev = &edict.v; edict.v.pContainingEntity = &edict; edict.pvPrivateData = &monster;
        edict.v.sequence = 4; edict.v.frame = 91; edict.v.framerate = 2; edict.v.animtime = 9;
        animation.SynchronizeSequence(4); animation.LockUntil(19);
        sequences[MonsterSequence::AttackWalk] = 10;
        sequences[MonsterSequence::AttackRun1] = 11;
        sequences[MonsterSequence::AttackRun2] = 12;
        activeAnimation = &animation; activeClocks = &clocks; resetCalls.clear();
        model.header.numseq = static_cast<int>(model.sequence.size());
        model.header.seqindex = static_cast<int>(reinterpret_cast<char*>(model.sequence.data()) - reinterpret_cast<char*>(&model));
        for (auto& sequence : model.sequence)
        {
            sequence.fps = 10; sequence.numframes = 11; sequence.flags = STUDIO_LOOPING;
            sequence.linearmovement[0] = 3; sequence.linearmovement[1] = 4;
        }
        activeModel = withModel ? &model : nullptr;
    }
    ~Fixture() { gpGlobals = nullptr; g_engfuncs = {}; activeAnimation = nullptr; activeClocks = nullptr; activeModel = nullptr; }
    bool Attack(float x, float y, float interval = 1.5f, bool boosted = false, float now = 25)
    {
        globals.time = now;
        return CSOL_StartMonsterMelee(monster, animation, sequences, x, y, now, interval, boosted, clocks);
    }
};

void AttackCases()
{
    struct Case { float x, y; bool run; };
    for (const auto row : {Case{0, 0, false}, Case{20, 0, false}, Case{12, 16, false},
        Case{std::nextafter(20.0f, 0.0f), 0, false}, Case{std::nextafter(20.0f, 30.0f), 0, true},
        Case{20, 1, true}, Case{-21, 0, true}})
    for (unsigned seed = 0; seed < 16; ++seed)
    {
        Fixture f;
        const auto oldClocks = ClockBits(f.clocks);
        std::srand(seed); const int first = std::rand(); const int second = std::rand();
        std::srand(seed);
        Check(f.Attack(row.x, row.y), "finite melee state accepts");
        const int next = std::rand();
        Check(next == (row.run ? second : first), "running alone consumes exactly one CRT random value");
        const int expectedSequence = row.run ? (first % 2 ? 12 : 11) : 10;
        Check(f.monster.pev->sequence == expectedSequence && f.animation.CachedSequence() == expectedSequence,
            "strict 400 threshold chooses walk or corresponding CRT-parity run");
        Check(f.animation.LockedUntil() == (row.run ? 25.5f : 26.0f), "walk and run use distinct animation locks");
        Check(f.monster.pev->frame == 0 && f.monster.pev->framerate == 1 && resetCalls.size() == 1,
            "new melee selection resets frame and calls host sequence reset once");
        Check(resetCalls[0].lock == Bits(row.run ? 25.5f : 26.0f) && resetCalls[0].clocks == oldClocks,
            "animation lock is published before SDK reset and interval clocks afterwards");
        Check(ClockBits(f.clocks) == std::array<std::uint32_t, 9>{Bits(1.5f), Bits(26.5f), Bits(25),
            Bits(1.5f), Bits(26.5f), Bits(25), Bits(1.5f), Bits(26.5f), Bits(25)}, "three independent clocks receive interval deadline stamp");
        Check(f.monster.m_flFrameRate == 256 && f.monster.m_flGroundSpeed == 5 && f.monster.m_fSequenceLoops
            && f.monster.pev->animtime == 25, "host SDK reset reads synthetic model and current time");
    }
    for (const int selected : {-3, -1, 4, 10})
    {
        Fixture f;
        f.sequences[MonsterSequence::AttackWalk] = static_cast<std::int16_t>(selected);
        const auto before = State(f.monster);
        Check(f.Attack(0, 0), "walk sentinel accepts");
        Check(f.animation.LockedUntil() == 26 && f.clocks.attack.deadline == 26.5f,
            "sentinels still refresh animation lock and interval clocks");
        if (selected == -3)
            Check(f.monster.pev->sequence == before.sequence && Bits(f.monster.pev->frame) == before.frame
                && Bits(f.monster.pev->framerate) == before.rate && resetCalls.empty(), "preserve sentinel retains model fields");
        else
            Check(f.monster.pev->sequence == (selected == -1 ? 0 : selected) && f.monster.pev->frame == 0
                && resetCalls.size() == (selected == 4 ? 0u : 1u), "missing maps to zero and same sequence skips SDK reset");
    }
    for (const int walk : {-3, -1, 10})
    {
        Fixture f;
        f.sequences[MonsterSequence::AttackWalk] = static_cast<std::int16_t>(walk);
        f.sequences[MonsterSequence::AttackRun1] = f.sequences[MonsterSequence::AttackRun2] = -1;
        Check(f.Attack(21, 0) && f.monster.pev->sequence == (walk == -3 ? 4 : walk == -1 ? 0 : walk),
            "missing run sequence falls back through walk sentinel semantics");
        Check(f.animation.LockedUntil() == 25.5f, "missing run sequence retains running lock");
    }
    for (const auto interval : {0.0f, 0.5f, 0.9f, 1.0f, 2.0f, -3.0f})
    {
        Fixture f;
        Check(f.Attack(0, 0, interval, true), "boosted finite interval accepts");
        const float expected = interval <= 0.9f ? 0.4f : interval == 1 ? 0.5f : 1.5f;
        Check(Bits(f.clocks.attack.interval) == Bits(expected), "boost clamps reduced interval to original float constant");
    }
}

void InvalidCases()
{
    struct Case { float x{}, y{}, now{25}, interval{1.5f}; };
    std::vector<Case> cases;
    for (const auto invalid : {0x7f800000u, 0xff800000u, 0x7fc12345u})
    {
        cases.push_back({Float(invalid), 0}); cases.push_back({0, Float(invalid)});
        cases.push_back({0, 0, Float(invalid)}); cases.push_back({0, 0, 25, Float(invalid)});
    }
    cases.push_back({std::numeric_limits<float>::max(), 0});
    cases.push_back({1.5e19f, 1.5e19f});
    cases.push_back({21, 0, std::numeric_limits<float>::max(), std::numeric_limits<float>::max()});
    for (const auto row : cases)
    {
        Fixture f;
        const auto before = State(f.monster);
        std::srand(2025); const int expectedRandom = std::rand(); std::srand(2025);
        Check(!f.Attack(row.x, row.y, row.interval, false, row.now), "nonfinite input or finite overflow rejects");
        Check(State(f.monster) == before && resetCalls.empty(), "rejection preserves model lock and every clock");
        Check(std::rand() == expectedRandom, "rejection consumes no CRT random state");
    }
}

int Oracle()
{
    unsigned seed{};
    while (std::cin >> seed)
    {
        int cache{}, sequence{}, walk{}, run1{}, run2{}, boosted{};
        std::uint32_t frame{}, rate{}, lock{}, now{}, x{}, y{}, interval{};
        if (!(std::cin >> cache >> sequence >> frame >> rate >> lock >> now >> x >> y >> interval
            >> boosted >> walk >> run1 >> run2)) return 2;
        Fixture f(false); f.animation.SynchronizeSequence(cache); if (!f.animation.LockUntil(Float(lock))) return 2;
        f.monster.pev->sequence = sequence; f.monster.pev->frame = Float(frame); f.monster.pev->framerate = Float(rate);
        f.sequences[MonsterSequence::AttackWalk] = static_cast<std::int16_t>(walk);
        f.sequences[MonsterSequence::AttackRun1] = static_cast<std::int16_t>(run1);
        f.sequences[MonsterSequence::AttackRun2] = static_cast<std::int16_t>(run2);
        std::srand(seed);
        const bool accepted = f.Attack(Float(x), Float(y), Float(interval), boosted != 0, Float(now));
        const auto state = State(f.monster);
        std::cout << accepted << ' ' << state.cache << ' ' << state.sequence << ' ' << state.frame << ' '
            << state.rate << ' ' << state.lock << ' ' << resetCalls.size();
        for (const auto& call : resetCalls)
        {
            std::cout << ' ' << call.sequence << ' ' << call.cache << ' ' << call.frame << ' ' << call.rate << ' ' << call.lock;
            for (const auto value : call.clocks) std::cout << ' ' << value;
        }
        for (const auto value : state.clocks) std::cout << ' ' << value;
        std::cout << ' ' << std::rand() << '\n';
    }
    return std::cin.eof() ? 0 : 2;
}
}

void CBaseAnimating::ResetSequenceInfo()
{
    resetCalls.push_back(State(*this));
    ResetSequenceInfo_OrigFunc();
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
    if (argc == 2 && std::string_view(argv[1]) == "--random")
    {
        unsigned seed{};
        while (std::cin >> seed)
        {
            std::srand(seed); const int first = std::rand(); const int second = std::rand();
            std::cout << first << ' ' << second << '\n';
        }
        return std::cin.eof() ? 0 : 2;
    }
    if (argc != 1) return 2;
    AttackCases(); InvalidCases();
    std::cout << "monster_attack checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
