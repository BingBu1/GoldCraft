// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace csol::movement
{
inline constexpr char PassKey[] = "cso_pass";

inline std::uint32_t ReadPassMask(const char* value)
{
    if (!value) return 0;
    std::uint32_t mask{};
    for (unsigned i = 0; i < 8; ++i)
    {
        const auto c = value[i];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0) return 0;
        mask = (mask << 4) | static_cast<unsigned>(digit);
    }
    return value[8] ? 0 : mask;
}

// Bind only for the duration of PM_Move. Keep the engine's physent order,
// ground/touch indices and weapon traces intact. The Ex callbacks are public
// GoldSrc APIs and also cover stuck-position checks and traces against visents.
class PlayerCollisionScope
{
    using Ignore = int (*)(physent_t*);
    playermove_t& move;
    std::uint32_t mask;
    decltype(playermove_t::PM_PlayerTrace) playerTrace;
    decltype(playermove_t::PM_TestPlayerPosition) testPosition;
    decltype(playermove_t::PM_TraceLine) traceLine;
    decltype(playermove_t::PM_PlayerTraceEx) playerTraceEx;
    decltype(playermove_t::PM_TestPlayerPositionEx) testPositionEx;
    decltype(playermove_t::PM_TraceLineEx) traceLineEx;
    PlayerCollisionScope* previous{};
    Ignore extraIgnore{};
    physent_t* ignoredEntity{};
    bool installed{};
    inline static thread_local PlayerCollisionScope* current{};

    struct Query
    {
        PlayerCollisionScope& owner;
        Ignore previousIgnore;
        physent_t* previousEntity;
        Query(PlayerCollisionScope& s, Ignore ignore, physent_t* entity = nullptr) :
            owner(s), previousIgnore(s.extraIgnore), previousEntity(s.ignoredEntity)
        { s.extraIgnore = ignore; s.ignoredEntity = entity; }
        ~Query() { owner.extraIgnore = previousIgnore; owner.ignoredEntity = previousEntity; }
    };
    static int Skip(physent_t* entity)
    {
        auto& s = *current;
        return entity && (entity == s.ignoredEntity ||
            (entity->player && entity->info >= 1 && entity->info <= 32 &&
                (s.mask & (std::uint32_t{1} << (entity->info - 1)))) ||
            (s.extraIgnore && s.extraIgnore(entity)));
    }
    static physent_t* Entity(playermove_t& p, int index, bool visible)
    {
        const int count = visible ? p.numvisent : p.numphysent;
        return index >= 0 && index < count ? (visible ? p.visents : p.physents) + index : nullptr;
    }
    static pmtrace_t PlayerTrace(float* start, float* end, int flags, int ignored)
    {
        auto& s = *current;
        const Query query(s, nullptr, Entity(s.move, ignored, false));
        return s.playerTraceEx(start, end, flags, Skip);
    }
    static pmtrace_t PlayerTraceEx(float* start, float* end, int flags, Ignore ignore)
    {
        auto& s = *current;
        const Query query(s, ignore);
        return s.playerTraceEx(start, end, flags, Skip);
    }
    static int TestPositionEx(float* position, pmtrace_t* trace, Ignore ignore)
    {
        auto& s = *current;
        const Query query(s, ignore);
        const int result = s.testPositionEx(position, nullptr, Skip);
        // ReHLDS's position callback fills ptrace through an unfiltered trace
        // before applying its ignore predicate. Its output trace describes the
        // current origin, even when checking an offset during stuck recovery.
        if (trace) *trace = s.playerTraceEx(s.move.origin, s.move.origin, PM_NORMAL, Skip);
        return result;
    }
    static int TestPosition(float* position, pmtrace_t* trace)
    { return TestPositionEx(position, trace, nullptr); }
    static pmtrace_t* TraceLine(float* start, float* end, int flags, int hull, int ignored)
    {
        auto& s = *current;
        const Query query(s, nullptr, Entity(s.move, ignored, flags != 0));
        return s.traceLineEx(start, end, flags, hull, Skip);
    }
    static pmtrace_t* TraceLineEx(float* start, float* end, int flags, int hull, Ignore ignore)
    {
        auto& s = *current;
        const Query query(s, ignore);
        return s.traceLineEx(start, end, flags, hull, Skip);
    }
public:
    PlayerCollisionScope(playermove_t& p, std::uint32_t peers) : move(p), mask(peers),
        playerTrace(p.PM_PlayerTrace), testPosition(p.PM_TestPlayerPosition), traceLine(p.PM_TraceLine),
        playerTraceEx(p.PM_PlayerTraceEx), testPositionEx(p.PM_TestPlayerPositionEx), traceLineEx(p.PM_TraceLineEx)
    {
        if (!mask || !playerTraceEx || !testPositionEx || !traceLineEx || current) return;
        previous = current;
        current = this;
        installed = true;
        p.PM_PlayerTrace = PlayerTrace; p.PM_PlayerTraceEx = PlayerTraceEx;
        p.PM_TestPlayerPosition = TestPosition; p.PM_TestPlayerPositionEx = TestPositionEx;
        p.PM_TraceLine = TraceLine; p.PM_TraceLineEx = TraceLineEx;
    }
    ~PlayerCollisionScope()
    {
        if (!installed) return;
        move.PM_PlayerTrace = playerTrace; move.PM_PlayerTraceEx = playerTraceEx;
        move.PM_TestPlayerPosition = testPosition; move.PM_TestPlayerPositionEx = testPositionEx;
        move.PM_TraceLine = traceLine; move.PM_TraceLineEx = traceLineEx;
        current = previous;
    }
    PlayerCollisionScope(const PlayerCollisionScope&) = delete;
    PlayerCollisionScope& operator=(const PlayerCollisionScope&) = delete;
    bool Active() const { return installed; }
};
}
