// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
struct edict_s;

// Called once per usercmd after native PlayerRunThink and before PM_Move.
// expectedMap is captured before PreThink, so a callback cannot cross a map.
void GoldCraftPlayerThinkStage(edict_s*, float frameTime, double clientTimebase,
                              std::uint32_t expectedMap, int expectedSerial) noexcept;
// Before retiring map memory or unloading game DLL callbacks.
void GoldCraftPlayerThinkStageReset() noexcept;
