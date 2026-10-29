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
    // Checks the current map and available rows before writing a next-map request.
    virtual bool RequestDifficulty(int mapId, int difficulty) = 0;
};
}
