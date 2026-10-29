// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../../native/csol/server/scenario_monster_animation.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
using namespace csol::scenario;
int checks{}, failures{};
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::int16_t Narrow(int value) { return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value)); }
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
struct ResetCall
{
    int sequence{}, cache{};
    std::uint32_t frame{}, rate{};
    bool operator==(const ResetCall&) const = default;
};
CCSOLMonsterAnimation* activeAnimation{};
std::vector<ResetCall> resetCalls;
std::vector<ResetCall> modelQueries;
void* activeModel{};
ResetCall State(CBaseAnimating& monster)
{
    return {monster.pev->sequence, activeAnimation->CachedSequence(), Bits(monster.pev->frame), Bits(monster.pev->framerate)};
}
void* ModelPointer(edict_t* edict)
{
    auto* monster = static_cast<CBaseAnimating*>(edict->pvPrivateData);
    modelQueries.push_back(State(*monster));
    return activeModel;
}
struct Fixture
{
    globalvars_t globals{};
    edict_t edict{};
    CBaseAnimating monster{};
    CCSOLMonsterAnimation animation;
    MonsterSequences sequences;
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t, 160> sequence{}; } model;
    Fixture(bool withModel = true)
    {
        gpGlobals = &globals; globals.time = 25;
        g_engfuncs = {}; g_engfuncs.pfnGetModelPtr = ModelPointer;
        monster.pev = &edict.v; edict.v.pContainingEntity = &edict; edict.pvPrivateData = &monster;
        edict.v.sequence = 4; edict.v.frame = 91; edict.v.framerate = 2; edict.v.animtime = 9;
        monster.m_flFrameRate = 7; monster.m_flGroundSpeed = 8;
        monster.m_flLastEventCheck = 9; monster.m_fSequenceFinished = TRUE; monster.m_fSequenceLoops = FALSE;
        animation.SynchronizeSequence(4); activeAnimation = &animation;
        resetCalls.clear(); modelQueries.clear();
        for (std::size_t i = 0; i < sequences.values.size(); ++i) sequences.values[i] = static_cast<std::int16_t>(100 + i);
        model.header.numseq = static_cast<int>(model.sequence.size());
        model.header.seqindex = static_cast<int>(reinterpret_cast<char*>(model.sequence.data()) - reinterpret_cast<char*>(&model));
        for (auto& sequence : model.sequence)
        {
            sequence.fps = 10; sequence.numframes = 11; sequence.flags = STUDIO_LOOPING;
            sequence.linearmovement[0] = 3; sequence.linearmovement[1] = 4;
        }
        activeModel = withModel ? &model : nullptr;
    }
    ~Fixture() { gpGlobals = nullptr; g_engfuncs = {}; activeAnimation = nullptr; activeModel = nullptr; }
    ResetCall State() { return ::State(monster); }
    bool Move(float x, float y, float now = 25, std::int16_t skill = 0, bool climb = false)
    {
        globals.time = now;
        return animation.UpdateLocomotion(monster, sequences, skill, x, y, now, climb);
    }
};

void SetterCases()
{
    Fixture f;
    f.animation.SetSequence(f.monster, 5, false);
    Check(f.State() == ResetCall{5, 5, Bits(91), Bits(1)}, "new sequence publishes cache and fields");
    Check(resetCalls == std::vector<ResetCall>{{5, 5, Bits(91), Bits(1)}}, "SDK reset observes published cache sequence rate and retained frame");
    Check(modelQueries.size() == 2 && modelQueries[0] == resetCalls[0] && modelQueries[1] == resetCalls[0], "real ResetSequenceInfo reads model for info and flags");
    Check(f.monster.pev->animtime == 25 && f.monster.m_flLastEventCheck == 25 && !f.monster.m_fSequenceFinished,
        "actual SDK reset refreshes timing and finished state");
    Check(f.monster.m_flFrameRate == 256 && f.monster.m_flGroundSpeed == 5 && f.monster.m_fSequenceLoops,
        "actual SDK reset consumes synthetic studio sequence fps movement and looping");
    f.globals.time = 30; f.monster.pev->framerate = 7;
    f.animation.SetSequence(f.monster, 5, false);
    Check(f.monster.pev->framerate == 1 && f.monster.pev->frame == 91 && resetCalls.size() == 1,
        "same sequence resets rate but does not reset sequence timing");
    Check(f.monster.pev->animtime == 25 && modelQueries.size() == 2, "same sequence performs no model query");
    f.animation.SetSequence(f.monster, 5, true);
    Check(f.monster.pev->frame == 0 && f.monster.pev->animtime == 25 && resetCalls.size() == 1,
        "same sequence resetFrame clears frame independently of ResetSequenceInfo");
    f.monster.pev->frame = 73; f.monster.pev->framerate = 9;
    const auto before = f.State();
    f.animation.SetSequence(f.monster, -3, true);
    Check(f.State() == before && resetCalls.size() == 1, "minus three preserves every selected field even with resetFrame");
    f.animation.SetSequence(f.monster, -1, true);
    Check(f.State() == ResetCall{0, 0, Bits(0), Bits(1)} && resetCalls.size() == 2, "minus one maps to sequence zero");
    f.monster.pev->frame = 100; f.globals.time = 31;
    Check(f.monster.StudioFrameAdvance(0.25f) == 0.25f && f.monster.pev->frame == 164,
        "real SDK frame advance consumes selected model FPS and runtime rate");
    f.monster.pev->frame = 250;
    f.monster.StudioFrameAdvance(0.25f);
    Check(f.monster.pev->frame == 58 && f.monster.m_fSequenceFinished, "real SDK looping wraps frame after advance");
}

void LocomotionCases()
{
    struct Case { float x, y; int selected; };
    for (const auto item : {Case{0, 0, 100}, Case{6, 0, 100}, Case{0, 6, 100}, Case{3, 4, 100},
        Case{6, 1, 101}, Case{20, 0, 101}, Case{12, 16, 101}, Case{20, 1, 102}, Case{0, -21, 102}})
    for (const std::int16_t skill : {std::int16_t{0}, std::int16_t{6}, std::int16_t{1002}})
    {
        Fixture f;
        Check(f.Move(item.x, item.y, 25, skill), "valid movement accepted");
        const int wanted = item.selected + (skill == 6 ? 7 : 0);
        Check(f.monster.pev->sequence == wanted && f.animation.CachedSequence() == wanted, "strict 36 and 400 thresholds and skill-six slots");
        Check(f.monster.pev->frame == 91 && resetCalls.size() == 1 && resetCalls[0].rate == Bits(1),
            "selector preserves frame and sets rate after SDK reset");
    }
    for (const bool climb : {false, true})
    {
        Fixture f;
        Check(f.Move(25, 0, 25, 0, climb), "run movement accepted");
        Check(resetCalls.size() == (climb ? 2u : 1u), "climb override occurs after ordinary selection");
        Check(resetCalls[0].sequence == 102 && (!climb || resetCalls[1].sequence == 130), "run then climb sequence order");
        Check(!climb || f.monster.pev->framerate == 1, "climb setter overrides prior movement rate");
    }
    Fixture f;
    f.animation.LockUntil(30);
    auto before = f.State();
    Check(f.Move(30, 0, std::nextafter(30.0f, 0.0f), 6, true) && f.State() == before && resetCalls.empty(),
        "strictly-before deadline suppresses all movement and climb setters");
    Check(f.Move(30, 0, 30, 6, false) && f.monster.pev->sequence == 109 && resetCalls.size() == 1,
        "deadline equality is unlocked");
    f.sequences[MonsterSequence::Run] = -3; f.animation.SynchronizeSequence(5);
    f.monster.pev->sequence = 5; f.monster.pev->framerate = 9; resetCalls.clear();
    Check(f.Move(25, 0) && f.monster.pev->sequence == 5 && f.animation.CachedSequence() == 5 && resetCalls.empty()
        && f.monster.pev->framerate != 9, "moving preserve sentinel still receives movement rate");
    f.sequences[MonsterSequence::Climb] = -3;
    Check(f.Move(25, 0, 25, 0, true) && resetCalls.empty(), "climb preserve sentinel retains moving rate and sequence");
    f.sequences[MonsterSequence::Idle] = -1;
    Check(f.Move(0, 0) && f.monster.pev->sequence == 0, "missing idle maps to zero through original setter");
}

void RejectAndCacheCases()
{
    for (const int raw : {-65537, -32769, -32768, -3, -1, 0, 32767, 32768, 65535, 65536, INT32_MAX, INT32_MIN})
    {
        Fixture f(false); f.animation.LockUntil(20); f.animation.SynchronizeSequence(raw);
        Check(f.animation.CachedSequence() == Narrow(raw) && f.animation.LockedUntil() == 0,
            "factory synchronization narrows signed16 and resets lock");
        Check(f.monster.pev->sequence == 4 && resetCalls.empty(), "synchronization only updates local cache");
    }
    for (const auto invalid : {0x7f800000u, 0xff800000u, 0x7fc12345u})
    {
        Fixture f; f.animation.LockUntil(30); const auto before = f.State();
        Check(!f.animation.LockUntil(Float(invalid)) && f.animation.LockedUntil() == 30, "nonfinite lock rejected without changing deadline");
        Check(!f.Move(1, 1, Float(invalid)) && f.State() == before && resetCalls.empty(), "nonfinite time rejects without changes");
        Check(f.Move(Float(invalid), Float(invalid), 25) && f.State() == before, "active lock returns before displacement validation");
        f.animation.LockUntil(0);
        Check(!f.Move(Float(invalid), 0) && !f.Move(0, Float(invalid)) && f.State() == before && resetCalls.empty(),
            "nonfinite displacement rejects before sequence writes");
    }
    Fixture f; const auto before = f.State();
    Check(!f.Move(std::numeric_limits<float>::max(), 0) && f.State() == before && resetCalls.empty(),
        "finite square overflow rejects with no mutation");
    Check(!f.Move(1.5e19f, 1.5e19f) && f.State() == before && resetCalls.empty(),
        "finite sum overflow rejects with no mutation");
    Check(f.Move(1.0e19f, 0) && std::isfinite(f.monster.pev->framerate), "large finite distance yields finite playback rate");
}

int Oracle()
{
    char kind{};
    while (std::cin >> kind)
    {
        int cache{}, sequence{}; std::uint32_t frame{}, rate{};
        if (!(std::cin >> cache >> sequence >> frame >> rate)) return 2;
        Fixture f(false); f.animation.SynchronizeSequence(cache);
        f.monster.pev->sequence = sequence; f.monster.pev->frame = Float(frame); f.monster.pev->framerate = Float(rate);
        bool accepted = true;
        if (kind == 'S')
        {
            int requested{}, reset{};
            if (!(std::cin >> requested >> reset)) return 2;
            f.animation.SetSequence(f.monster, requested, reset != 0);
        }
        else if (kind == 'L')
        {
            std::uint32_t lock{}, now{}, x{}, y{}; int skill{}, climb{};
            if (!(std::cin >> lock >> now >> x >> y >> skill >> climb)) return 2;
            for (const auto slot : {MonsterSequence::Idle, MonsterSequence::Walk, MonsterSequence::Run,
                MonsterSequence::AimGrenadeIdle, MonsterSequence::AimGrenadeWalk, MonsterSequence::AimGrenadeRun,
                MonsterSequence::Climb})
            {
                int value{}; if (!(std::cin >> value)) return 2;
                f.sequences[slot] = Narrow(value);
            }
            if (!f.animation.LockUntil(Float(lock))) return 2;
            accepted = f.Move(Float(x), Float(y), Float(now), Narrow(skill), climb != 0);
        }
        else return 2;
        const auto state = f.State();
        std::cout << accepted << ' ' << state.cache << ' ' << state.sequence << ' ' << state.frame << ' ' << state.rate << ' ' << resetCalls.size();
        for (const auto& call : resetCalls) std::cout << ' ' << call.sequence << ' ' << call.cache << ' ' << call.frame << ' ' << call.rate;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 2;
}
}

// The real SDK ResetSequenceInfo implementation runs unchanged. Only the
// registry dispatch is replaced by this observable direct call to its OrigFunc;
// third-party ReAPI hooks and hook ordering are outside this target.
void CBaseAnimating::ResetSequenceInfo()
{
    resetCalls.push_back(State(*this));
    ResetSequenceInfo_OrigFunc();
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
    if (argc != 1) return 2;
    SetterCases(); LocomotionCases(); RejectAndCacheCases();
    std::cout << "monster_animation checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
