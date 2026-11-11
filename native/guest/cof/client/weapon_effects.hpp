// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "presentation.hpp"

struct cl_enginefuncs_s;
struct cl_entity_s;
struct mstudioevent_s;

namespace guest::cof::client
{
enum class EffectResult { Dispatched, Unhandled, InvalidEntity, InvalidOptions, MissingBackend, BackendFailed };

struct StudioHost
{
    cl_enginefuncs_s* engine{};
    void* context{};
    bool (*localSound)(void*, const LocalSound&) noexcept {};
};

// Stateless original Studio-event consumer. The host authenticates the current
// weapon/model and invokes this from the actual Studio event, on its owner thread.
// Dispatched means an SDK request was issued, not rendered/server-accepted.
class WeaponEffects
{
public:
    explicit WeaponEffects(StudioHost host) noexcept : host_(host) {}
    EffectResult StudioEvent(const mstudioevent_s& event, const cl_entity_s& entity,
                             Weapon currentWeapon) const noexcept;

private:
    StudioHost host_;
};
}
