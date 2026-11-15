// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "player_think_stage.h"
#include "goldcraft/player_think_stage_api.hpp"
#include <cmath>
#include <limits>

namespace
{
class PlayerThinkStage final : public goldcraft::IPlayerThinkStage
{
public:
    std::uint64_t Register(void* context, goldcraft::PlayerThinkStageCallback callback) noexcept override
    {
        if (busy_ || !callback || !MapAvailable() ||
            (callback_ && map_ == std::uint32_t(g_psvs.spawncount)) ||
            serial_ == std::numeric_limits<std::uint64_t>::max()) return 0;
        context_ = context; callback_ = callback;
        map_ = std::uint32_t(g_psvs.spawncount); token_ = ++serial_;
        return token_;
    }
    bool Current(std::uint64_t token) const noexcept override
    {
        return token && token == token_ && callback_ && MapAvailable() &&
            map_ == std::uint32_t(g_psvs.spawncount);
    }
    bool Release(std::uint64_t token) noexcept override
    {
        if (!token || token != token_) return false;
        Clear(); return true;
    }
    // Map/error teardown can abandon the current callback through Host_Error.
    void Reset() noexcept { Clear(); busy_ = false; }
    void Run(edict_t* player, float frameTime, double clientTimebase, std::uint32_t expectedMap, int expectedSerial) noexcept
    {
        if (!callback_ || busy_ || g_psv.state != ss_active ||
            expectedMap != map_ || expectedMap != std::uint32_t(g_psvs.spawncount) ||
            !player || player->free || player->serialnumber != expectedSerial || (player->v.flags & (FL_KILLME | FL_DORMANT)) ||
            !std::isfinite(frameTime) || frameTime < 0 || !std::isfinite(clientTimebase)) return;
        const auto callback = callback_; auto* context = context_;
        busy_ = true;
        callback(context, player, frameTime, clientTimebase);
        busy_ = false;
    }
private:
    static bool MapAvailable() noexcept { return g_psv.state == ss_loading || g_psv.state == ss_active; }
    void Clear() noexcept { context_ = nullptr; callback_ = nullptr; token_ = 0; map_ = 0; }
    void* context_{};
    goldcraft::PlayerThinkStageCallback callback_{};
    std::uint64_t token_{}, serial_{};
    std::uint32_t map_{};
    bool busy_{};
};
PlayerThinkStage stage;
IBaseInterface* CreateStage() { return &stage; }
}
using goldcraft::IPlayerThinkStage;
EXPOSE_INTERFACE_FN(CreateStage, IPlayerThinkStage, goldcraft::PlayerThinkStageVersion);

void GoldCraftPlayerThinkStage(edict_t* player, float frameTime, double clientTimebase,
                              std::uint32_t expectedMap, int expectedSerial) noexcept
{ stage.Run(player, frameTime, clientTimebase, expectedMap, expectedSerial); }
void GoldCraftPlayerThinkStageReset() noexcept { stage.Reset(); }
