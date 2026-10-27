// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cbase.h"
#include "../scenario/config.hpp"

class CSOLMonsterSpawnCall;

// Host state, not behavior state: ordinary pool reuse preserves these fields.
struct CSOLMonsterGaitState
{
    int sequence{};
    float frame{}, yaw{}, estimatedYaw{};
    Vector previousOrigin{};
    float pitchController{}, yawController{}, distance{}, turnRate{};
};

bool CSOL_UpdateMonsterGait(CSOLMonsterSpawnCall& call, csol::scenario::ConfigError& error,
    bool (*ownerValid)(const void*) = nullptr, const void* ownerContext = nullptr);
