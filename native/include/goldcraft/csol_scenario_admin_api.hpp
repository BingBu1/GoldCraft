// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// Local x86 CreateInterface contract; include the matching interface.h first.
namespace csol
{
constexpr const char* ScenarioAdminServiceVersion = "CSOLScenarioAdmin001";
struct ScenarioDifficultyInfo
{
    std::int32_t mapId{}, active{}, requested{}, maximum{};
};
static_assert(sizeof(ScenarioDifficultyInfo) == 16);

class IScenarioAdminService : public IBaseInterface
{
public:
    // Values from one current map snapshot. requested=0 denotes an invalid
    // pending CVAR; active remains the difficulty used by all loaded services.
    virtual bool DifficultyInfo(ScenarioDifficultyInfo& output) const = 0;
    // Trusted server-plugin call. Pawn must enforce administrator permissions.
    // Checks current map/rows before writing the next replay/map request.
    virtual bool RequestDifficulty(int mapId, int difficulty) = 0;
};

// Separate extension preserves Admin001's deployed vtable contract.
constexpr const char* ScenarioReplayServiceVersion = "CSOLScenarioReplay001";
class IScenarioReplayService : public IBaseInterface
{
public:
    // Queues a complete new challenge on this loaded map. Work executes at
    // the next safe GameRules frame; true is acceptance, not completion.
    virtual bool RequestReplay(int mapId) = 0;
};

// User-requested GoldCraft administration; not an original CSOL rule.
// A separate extension preserves the deployed Admin001/Replay001 vtables.
constexpr const char* ScenarioMapVoteServiceVersion = "CSOLScenarioMapVote001";
class IScenarioMapVoteService : public IBaseInterface
{
public:
    // Trusted plugin call: Pawn checks administrator permissions. True only
    // accepts a request for the current map; a safe rules frame starts voting.
    virtual bool RequestMapVote(int mapId) = 0;
};
}
