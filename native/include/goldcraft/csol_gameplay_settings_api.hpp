// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// Local x86 service. The matching interface.h must be included first.
namespace csol
{
inline constexpr char GameplaySettingsServiceVersion[] = "CSOLGameplaySettings001";
enum class GameplayValueType : std::int32_t { Boolean, Integer, Real };
enum class GameplayEditResult : std::int32_t
{ Failed = -1, Stale = -2, Busy = -3, Unavailable = -4, Queued = 1, Saved = 2 };
struct GameplaySetting
{
    std::int32_t id{}, nextMap{}, available{};
    GameplayValueType type{};
    double value{}, minimum{}, maximum{};
    char key[64]{}, cvar[80]{}, comment[512]{};
};
struct GameplaySettingsStatus
{
    std::uint32_t revision{};
    std::int32_t count{}, busy{};
    GameplayEditResult result{GameplayEditResult::Unavailable};
    char error[256]{};
};
class IGameplaySettingsService : public IBaseInterface
{
public:
    virtual bool Status(GameplaySettingsStatus& output) = 0;
    virtual bool Read(int id, GameplaySetting& output) = 0;
    virtual int UserId(int player) const = 0;
    // Trusted local server-plugin calls. The AMXX bridge verifies live
    // actor/userid/ADMIN_CFG at submission; clients cannot call this ABI.
    virtual GameplayEditResult Edit(int actor, int userid, std::uint32_t revision, int id, double value) = 0;
};
}
