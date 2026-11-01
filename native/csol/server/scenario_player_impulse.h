// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../scenario/config.hpp"
#include <memory>
class CCSOLScenarioSession;
class CBaseEntity;
class Vector;
template<typename T> class EntityHandle;
using EHANDLE = EntityHandle<CBaseEntity>;

// True reports a handled request; applied=false is a normal state rejection.
// A projectile can outlive its boss, so ownership belongs to the map/player.
bool CSOL_ApplyScenarioPlayerImpulse(const std::shared_ptr<CCSOLScenarioSession>& session,
    const EHANDLE& target, const Vector& requested, bool& applied,
    csol::scenario::ConfigError& error);
