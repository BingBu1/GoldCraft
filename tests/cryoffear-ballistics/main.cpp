// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK types and production adapter. Only engine/entity edges are controlled.
#include "precompiled.h"
#include "native/guest/cof/server/ballistics.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals{};
MULTIDAMAGE gMultiDamage{};
bool g_bAllowedCSBot{};

namespace
{
std::vector<std::uint32_t> events;
std::array<edict_t, 6> edicts{};
int players{}, finalTarget{}, water{}, traces{}, checks{};
float surface{};
bool valid = true;
int cancelEvent{}, recycleEvent{}, recycleEntity{}, phase{}, privateAction{};
void* replacementPrivate{};
void (*nested)(){};

std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float Float(std::uint32_t value) { return std::bit_cast<float>(value); }
template<class... T> void Event(std::uint32_t kind, T... values)
{
    events.insert(events.end(), {kind, sizeof...(T), static_cast<std::uint32_t>(values)...});
    if (cancelEvent == static_cast<int>(kind)) valid = false;
    if (recycleEvent == static_cast<int>(kind))
    {
        if (privateAction) edicts[recycleEntity].pvPrivateData = privateAction == 1 ? nullptr : replacementPrivate;
        else ++edicts[recycleEntity].serialnumber;
        recycleEvent = 0;
    }
}
int Index(const edict_t* edict) { return static_cast<int>(edict - edicts.data()); }
int Index(const entvars_t* pev) { return Index(pev->pContainingEntity); }
void Check(bool condition)
{
    ++checks;
    if (!condition) { std::cerr << "SDK check " << checks << " failed phase " << phase << "\n"; std::exit(2); }
}
struct Target final : CBaseEntity
{
    int index{};
    BOOL IsPlayer() override { return index == 1 || (index >= 2 && index < 2 + players); }
    CBaseMonster* MyMonsterPointer() override
    {
        return index ? reinterpret_cast<CBaseMonster*>(this) : nullptr;
    }
    void TraceAttack(entvars_t* attacker, float damage, Vector direction, TraceResult* trace, int flags) override
    {
        Event(3, index, Index(attacker), Bits(damage), Bits(direction.x), Bits(direction.y), Bits(direction.z), Bits(trace->flFraction), flags);
        gMultiDamage.hEntity = this;
        gMultiDamage.amount = damage;
        if (nested) nested();
    }
    BOOL TakeDamage(entvars_t* inflictor, entvars_t* attacker, float damage, int flags) override
    {
        Event(4, index, Index(inflictor), Index(attacker), Bits(damage), flags);
        return TRUE;
    }
};
std::array<Target, 6> targets;
void Initialize()
{
    for (int i = 0; i < 6; ++i)
    {
        targets[i].index = i;
        targets[i].pev = &edicts[i].v;
        edicts[i].v.pContainingEntity = &edicts[i];
        edicts[i].pvPrivateData = &targets[i];
        edicts[i].v.solid = i == 0 ? SOLID_BSP : SOLID_SLIDEBOX;
        edicts[i].serialnumber = 1;
    }
    g_engfuncs.pfnIndexOfEdict = Index;
    g_engfuncs.pfnPEntityOfEntOffset = [](int) { return &edicts[0]; };
    g_engfuncs.pfnEntOffsetOfPEntity = [](const edict_t* entity) { return Index(entity); };
    g_engfuncs.pfnTraceLine = [](const float* start, const float* end, int ignore, edict_t* skip, TraceResult* trace)
    {
        Check(ignore == 0);
        Event(2, Index(skip), Bits(start[0]), Bits(start[1]), Bits(start[2]), Bits(end[0]), Bits(end[1]), Bits(end[2]));
        ++traces;
        const bool player = traces <= players;
        const int index = player ? traces + 1 : (finalTarget == 0 ? 0 : 5);
        *trace = {};
        trace->fInWater = water == 2;
        trace->flFraction = player || finalTarget != 2 ? 0.5f : 1.0f;
        for (int i = 0; i < 3; ++i)
            trace->vecEndPos[i] = start[i] + (end[i] - start[i]) * trace->flFraction;
        trace->vecPlaneNormal = {0, 0, 1};
        trace->pHit = &edicts[index];
    };
    g_engfuncs.pfnPointContents = [](const float* point)
    {
        Event(12, Bits(point[0]), Bits(point[1]), Bits(point[2]));
        return water != 0 && point[2] < surface ? CONTENTS_WATER : CONTENTS_EMPTY;
    };
    g_engfuncs.pfnMessageBegin = [](int destination, int id, const float*, edict_t*) { Event(6, destination, id); };
    g_engfuncs.pfnMessageEnd = [] { Event(11); };
    g_engfuncs.pfnWriteByte = [](int value) { Event(8, value); };
    g_engfuncs.pfnWriteShort = [](int value) { Event(9, value); };
    g_engfuncs.pfnWriteCoord = [](float value) { Event(7, Bits(value)); };
    g_engfuncs.pfnWriteString = [](const char* value) { Check(std::string_view(value) == "shot"); Event(10, 1); };
}
bool Current(void*) noexcept { return valid; }
}

// Controlled SDK call boundaries, never replacements for the production types.
void ClearMultiDamage() { Event(1); gMultiDamage = {}; }
void ApplyMultiDamage(entvars_t* inflictor, entvars_t* attacker)
{
    Event(5, Index(inflictor), Index(attacker));
    if (auto* entity = gMultiDamage.hEntity.Get<CBaseEntity>())
        entity->TakeDamage(inflictor, attacker, gMultiDamage.amount, gMultiDamage.type);
}
void UTIL_TraceLine(const Vector& start, const Vector& end, IGNORE_MONSTERS ignore, edict_t* skip, TraceResult* trace)
{
    g_engfuncs.pfnTraceLine(start, end, ignore, skip, trace);
}
int UTIL_PointContents(const Vector& position) { return g_engfuncs.pfnPointContents(position); }

// Unreached base virtual definitions satisfy the real SDK vtable in this fixture.
int CBaseEntity::Save(CSave&) { std::abort(); }
int CBaseEntity::Restore(CRestore&) { std::abort(); }
void CBaseEntity::SetObjectCollisionBox() { std::abort(); }
void CBaseEntity::TraceAttack(entvars_t*, float, Vector, TraceResult*, int) { std::abort(); }
BOOL CBaseEntity::TakeDamage(entvars_t*, entvars_t*, float, int) { std::abort(); }
BOOL CBaseEntity::TakeHealth(float, int) { std::abort(); }
void CBaseEntity::Killed(entvars_t*, int) { std::abort(); }
void CBaseEntity::TraceBleed(float, Vector, TraceResult*, int) { std::abort(); }
int CBaseEntity::DamageDecal(int) { std::abort(); }
void CBaseEntity::OnCreate() { std::abort(); }
void CBaseEntity::OnDestroy() { std::abort(); }
BOOL CBaseEntity::IsInWorld() { std::abort(); }
CBaseEntity* CBaseEntity::GetNextTarget() { std::abort(); }
BOOL CBaseEntity::FVisible(CBaseEntity*) { std::abort(); }
BOOL CBaseEntity::FVisible(const Vector&) { std::abort(); }
const Vector g_vecZero{};

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    Initialize();
    std::ifstream input(argv[1]), expected(argv[2]);
    if (!input || !expected) return 1;
    using namespace guest::cof;
    const BallisticsEffects fx{77, 13};
    const BallisticsCall call{Current, nullptr};
    std::size_t rows{};
    while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
    {
        std::array<std::uint32_t, 22> row{};
        for (auto& value : row) if (!(input >> std::hex >> value)) return 3;
        const auto vector = [&](int offset) { return Vector(Float(row[offset]), Float(row[offset+1]), Float(row[offset+2])); };
        PistolBallistics shot{vector(0), vector(3), vector(6), vector(9), vector(12), row[15], static_cast<int>(row[16]), row[17] != 0,
            PistolPlayerHits::OriginalCooperativeSkipPlayers, PistolSource::NormalMuzzle};
        players = row[18]; finalTarget = row[19]; water = row[20]; surface = Float(row[21]);
        events.clear(); traces = 0; valid = true; cancelEvent = 0;
        gMultiDamage = {}; gMultiDamage.amount = 123; gMultiDamage.type = 456;
        const auto result = FirePistolBallistics(targets[1], shot, fx, call);
        Check(result.status == BallisticsStatus::Fired);
        Check(gMultiDamage.amount == 123 && gMultiDamage.type == 456);
        std::string line;
        if (!std::getline(expected, line)) return 4;
        std::istringstream values(line);
        std::vector<std::uint32_t> wanted;
        std::uint32_t value{};
        while (values >> std::hex >> value) wanted.push_back(value);
        std::vector<std::uint32_t> actual{Bits(result.spread.x), Bits(result.spread.y), Bits(result.spread.z)};
        actual.insert(actual.end(), events.begin(), events.end());
        if (actual != wanted)
        {
            std::cerr << "Original instruction mismatch row " << rows << " actual/expected words " << actual.size() << '/' << wanted.size() << '\n';
            for (std::size_t i=0;i<std::min(actual.size(), wanted.size());++i)
                if (actual[i]!=wanted[i]) { std::cerr << "word " << i << ' ' << std::hex << actual[i] << " != " << wanted[i] << '\n'; break; }
            return 5;
        }
        // Unsupported/preflight calls must not touch engine or damage state.
        events.clear(); shot.sourceMode = PistolSource::UnsupportedOriginalOverride;
        Check(FirePistolBallistics(targets[1], shot, fx, call).status == BallisticsStatus::UnsupportedSource && events.empty());
        shot.sourceMode = PistolSource::NormalMuzzle;
        Check(FirePistolBallistics(targets[1], shot, {0,13}, call).status == BallisticsStatus::MissingEffects && events.empty());
        // Cancellation after TraceLine suppresses all following effects/damage.
        traces=0; cancelEvent=2; valid=true;
        Check(FirePistolBallistics(targets[1], shot, fx, call).status == BallisticsStatus::Cancelled);
        Check(events.size() == 11 && events[0] == 1 && events[2] == 2);
        ++rows;
    }
    if (rows != 512) return 6;
    input >> std::ws; expected >> std::ws;
    if (input.peek() != std::char_traits<char>::eof() || expected.peek() != std::char_traits<char>::eof()) return 7;

    const PistolBallistics baseline{{1,2,3}, {1,0,0}, {.02f,.02f,.02f}, {0,1,0}, {0,0,1},
        19, 35, false, PistolPlayerHits::OriginalCooperativeSkipPlayers, PistolSource::NormalMuzzle};
    const auto reset = [&]
    {
        events.clear(); traces=0; valid=true; cancelEvent=0; recycleEvent=0; privateAction=0;
        for (int i=0;i<6;++i) edicts[i].pvPrivateData=&targets[i];
        players=0; finalTarget=0; water=0; surface=200;
        gMultiDamage={}; gMultiDamage.amount=123; gMultiDamage.type=456;
    };
    // Cancellation stops callbacks after the current edge; begun packets finish.
    for (int edge : {1,2,3,4,5,6,7,8,9,10,11,12})
    {
        reset(); phase=edge; players=1; water=1; cancelEvent=edge;
        const auto cancelled=FirePistolBallistics(targets[1], baseline, fx, call);
        Check(cancelled.status == BallisticsStatus::Cancelled);
        Check(gMultiDamage.amount == 123 && gMultiDamage.type == 456);
        int messages=0, edgeCount=0; bool after=false;
        for (std::size_t at=0;at<events.size();)
        {
            const int kind=events[at], count=events[at+1];
            if (kind == 6) ++messages;
            if (kind == 11) --messages;
            if (kind == edge) { ++edgeCount; after=true; }
            else if (after && messages == 0) Check(kind == 11 || (edge == 5 && kind == 4));
            at += 2+count;
        }
        Check(edgeCount >= 1 && messages == 0);
    }
    // Recycled owner or damage target cancels before following effects.
    for (int action : {0,1,2})
    for (const auto [edge, entity] : std::array<std::array<int,2>,3>{{{2,1},{4,2},{3,0}}})
    {
        reset(); players=1; privateAction=action; replacementPrivate=&targets[4]; recycleEvent=edge; recycleEntity=entity;
        Check(FirePistolBallistics(targets[1], baseline, fx, call).status == BallisticsStatus::Cancelled);
        Check(gMultiDamage.amount == 123 && gMultiDamage.type == 456);
    }
    for (int invalid=0;invalid<8;++invalid)
    {
        reset(); auto shot=baseline; auto effects=fx; auto scope=call;
        switch(invalid)
        {
        case 0: shot.source.x=std::numeric_limits<float>::quiet_NaN(); break;
        case 1: shot.spread.y=std::numeric_limits<float>::infinity(); break;
        case 2: shot.damage=-1; break;
        case 3: scope.current=nullptr; break;
        case 4: shot.playerHits=static_cast<PistolPlayerHits>(255); break;
        case 5: effects.customDecalMessage=256; break;
        case 6: effects.bubbleModel=0; break;
        case 7: valid=false; break;
        }
        const auto rejected=FirePistolBallistics(targets[1], shot, effects, scope);
        Check(rejected.status != BallisticsStatus::Fired && rejected.status != BallisticsStatus::Ready);
        Check(events.empty() && gMultiDamage.amount==123 && gMultiDamage.type==456);
    }
    // A genuine recursive adapter call must preserve outer pending damage.
    reset(); finalTarget=5;
    static const PistolBallistics* recursiveShot;
    recursiveShot=&baseline;
    nested=[]
    {
        nested=nullptr;
        const auto saved=gMultiDamage;
        const int oldTraces=traces, oldFinal=finalTarget;
        traces=0; finalTarget=0;
        const auto inner=FirePistolBallistics(targets[1], *recursiveShot, {77,13}, {Current,nullptr});
        Check(inner.status == BallisticsStatus::Fired);
        Check(gMultiDamage.hEntity.Get<CBaseEntity>() == saved.hEntity.Get<CBaseEntity>() &&
            gMultiDamage.amount == saved.amount && gMultiDamage.type == saved.type);
        traces=oldTraces; finalTarget=oldFinal;
    };
    Check(FirePistolBallistics(targets[1], baseline, fx, call).status == BallisticsStatus::Fired);
    Check(gMultiDamage.amount==123 && gMultiDamage.type==456);
    Check(events.size()>=7 && events[events.size()-7]==4 && events[events.size()-5]==5);

    std::cout << rows << " original full-function cases; " << checks << " SDK checks; 0 failures\n";
}
