// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "guest_cof_factory.hpp"
#include <limits>

namespace csol::server::cof
{
namespace
{
guest::cof::server::GlockColdHost host;
std::uint64_t binding{1};
void Advance() noexcept
{
    if (binding) binding = binding == std::numeric_limits<std::uint64_t>::max() ? 0 : binding + 1;
}
}
bool InstallGlockFactory(guest::cof::server::GlockColdHost candidate) noexcept
{
    // Installing callbacks does not prove complete backend/resource readiness.
    // The class validates actual preparation separately before use.
    const auto before = binding;
    if (!before || !candidate.current || !candidate.prepare || !candidate.ownerBackends ||
        !candidate.inventoryPickup || !candidate.current(candidate.context) || binding != before) return false;
    Advance();
    if (!binding) return false;
    host = candidate;
    return true;
}
void ResetGlockFactory() noexcept { Advance(); host = {}; }
guest::cof::server::GlockWeapon* CreateGlock(entvars_t& vars) noexcept
{
    using guest::cof::server::GlockWeapon;
    auto* entity = ENT(&vars);
    if (!entity || entity->free || entity->v.flags & FL_KILLME) return nullptr;
    if (auto* existing = static_cast<CBaseEntity*>(entity->pvPrivateData))
    {
        auto* weapon = dynamic_cast<GlockWeapon*>(existing);
        return weapon && weapon->pev == &vars &&
            dynamic_cast<CCSPlayerWeapon*>(weapon->m_pEntity) &&
            weapon->m_pEntity->m_pContainingEntity == weapon ? weapon : nullptr;
    }
    const auto candidate = host;
    const auto generation = binding;
    const auto serial = entity->serialnumber;
    const auto entityCurrent = [&] {
        return binding && binding == generation && !entity->free && entity->serialnumber == serial &&
            !(entity->v.flags & FL_KILLME);
    };
    if (!candidate.current || !candidate.current(candidate.context) || !entityCurrent() ||
        entity->pvPrivateData) return nullptr;
    try
    {
        auto* weapon = GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<GlockWeapon*>(&vars));
        if (!weapon || !entityCurrent() || entity->pvPrivateData != weapon ||
            !candidate.current(candidate.context) || !entityCurrent() || entity->pvPrivateData != weapon) return nullptr;
        weapon->ConfigureColdHost(candidate);
        return weapon;
    }
    catch (...) { return nullptr; }
}
}

// Kept outside the public registration/precache lists until the authenticated
// map owner installs the complete, actual resource and effect consumers.
C_DLLEXPORT void EXT_FUNC weapon_cof_glock(entvars_t* vars);
void weapon_cof_glock(entvars_t* vars)
{
    if (vars) csol::server::cof::CreateGlock(*vars);
}
