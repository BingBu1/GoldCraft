// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

struct edict_s;
// Include matching interface.h first. Same-process, main server thread ABI.
namespace goldcraft
{
inline constexpr char PlayerThinkStageVersion[] = "GoldCraftPlayerThinkStage001";
using PlayerThinkStageCallback = void (*)(void*, edict_s*, float frameTime, double clientTimebase) noexcept;
class IPlayerThinkStage : public IBaseInterface
{
public:
    // One map-scoped consumer. Cold registration only; token zero means absent.
    virtual std::uint64_t Register(void* context, PlayerThinkStageCallback callback) noexcept = 0;
    virtual bool Current(std::uint64_t token) const noexcept = 0;
    virtual bool Release(std::uint64_t token) noexcept = 0;
};
}
