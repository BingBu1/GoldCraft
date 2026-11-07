// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_oberon_attacks.h"
#include "../core/scenario_runtime.h"
#include "../core/scenario_session.h"
#include "../core/scenario_player_runtime.h"
#include "../core/scenario_player_impulse.h"
#include "../core/scenario_equipment.h"
#include "../core/game_rules_gate.h"
#include <cstdlib>
#include <functional>

namespace
{
using csol::scenario::oberon::Position;
Position Coordinates(const Vector& vector) { return {vector.x, vector.y, vector.z}; }
struct PlayerCall
{
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    const CSOLOberonAttackGuard& source;
    const CSOLScenarioSdkScope& sdk;
    EHANDLE target;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> life;
    std::uint64_t round;
    bool OwnerCurrent() const
    {
        return sdk.Current() && runtime && !runtime->Closing() && runtime->session == session &&
            runtime->players == players && session->Map().RoundGeneration() == round && source.Current();
    }
    CBasePlayer* Current() const
    {
        if (!OwnerCurrent() || !life || players->MonsterSkillLife(target) != life) return nullptr;
        auto* player = dynamic_cast<CBasePlayer*>(target.Get<CBaseEntity>());
        return player && !player->has_disconnected && !(player->pev->flags & FL_KILLME) ? player : nullptr;
    }
};
bool Impulse(PlayerCall& call, const csol::scenario::oberon::Hit& hit, csol::scenario::ConfigError& error)
{
    if (!call.Current()) return true;
    bool applied{};
    const bool result = CSOL_ApplyScenarioPlayerImpulse(call.session, call.target,
        Vector(hit.impulse.data()), applied, error);
    return !call.OwnerCurrent() || !call.Current() || result;
}
bool Impact(PlayerCall& call, bool stun, bool shakeFirst, csol::scenario::ConfigError& error)
{
    auto* player = call.Current();
    if (!player) return true;
    if (shakeFirst)
    {
        if (!call.session->Messages().SendMonsterSkillShake(player->edict()))
        { error.message = "Oberon screen-shake message failed"; return false; }
        player = call.Current();
        if (!player) return true;
    }
    if (!call.session->Messages().SendOberonImpact(player->edict(), false))
    { error.message = "Oberon view-kick message failed"; return false; }
    player = call.Current();
    if (!player) return true;
    if (!shakeFirst && !call.session->Messages().SendMonsterSkillShake(player->edict()))
    { error.message = "Oberon screen-shake message failed"; return false; }
    player = call.Current();
    if (stun && player && !call.session->Messages().SendOberonImpact(player->edict(), true))
    { error.message = "Oberon stun sound message failed"; return false; }
    return true;
}
bool ShockDrop(PlayerCall& call, csol::scenario::ConfigError& error)
{
    auto* player = call.Current();
    if (!player || std::rand() % 100 >= 25) return true;
    const auto tables = call.session->Equipment();
    const auto* map = call.session->Map().Tables();
    const auto equipment = call.session->EquipmentState();
    if (!tables || !tables->weaponDropDifficulty || !map || !map->mapId || !equipment)
    { error.message = "Oberon shock-drop requires real equipment/map rules"; return false; }
    if (tables->WeaponDropRestricted(*map->mapId, call.session->Difficulty().SelectedRow())) return true;
    float resistance{};
    if (!equipment->WeaponDropResistance(call.target, resistance))
    { if (call.Current()) error.message = "Oberon shock-drop resistance is unavailable"; return false; }
    player = call.Current();
    if (!player || call.session->EquipmentState() != equipment) return true;
    static_assert(RAND_MAX == 32767);
    const float roll = float(std::rand()) * (1.0f / 32767.0f);
    if (!(roll > resistance))
    {
        ClientPrint(player->pev, HUD_PRINTCENTER, "#CSO_ZBSVER2_YouResistedWeaponDrop");
        return true;
    }
    if (call.players->MonsterWeaponDropBlocked(call.target, gpGlobals->time)) return true;
    auto* weapon = player->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT];
    if (!weapon) weapon = player->m_rgpPlayerItems[PISTOL_SLOT];
    if (!weapon) return true;
    const std::string name = STRING(weapon->pev->classname);
    player = call.Current();
    if (!player) return true;
    auto* dropped = player->DropPlayerItem(name.c_str());
    player = call.Current();
    if (!player) return true;
    // The native hook can return a now-freed box. Find its live edict before
    // dereferencing it, then apply ShockDrop's default speed400.
    for (int i = gpGlobals->maxClients + 1; dropped && i < gpGlobals->maxEntities; ++i)
    {
        auto* entry = INDEXENT(i);
        player = call.Current();
        if (!player) return true;
        if (!entry || entry->free || entry->pvPrivateData != dropped) continue;
        auto* box = dynamic_cast<CWeaponBox*>(dropped);
        if (box && !(box->pev->flags & FL_KILLME))
        {
            const EHANDLE handle(box);
            UTIL_MakeVectors(player->pev->angles);
            player = call.Current();
            if (!player) return true;
            if (handle.Get<CBaseEntity>() == box && !(box->pev->flags & FL_KILLME))
                box->pev->velocity = gpGlobals->v_forward * 400.0f;
        }
        break;
    }
    // 114EEFA0 returns true after calling DropItem, even if DropItem refused.
    player = call.Current();
    if (player) ClientPrint(player->pev, HUD_PRINTCENTER, "#CSO_ZBS_ShockDrop");
    return true;
}
enum class Attack { Sector, Cylinder, Bomb };
bool Apply(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const CSOLOberonAttackGuard& source,
    Attack kind, int team,
    const std::function<std::optional<csol::scenario::oberon::Hit>(const Vector&)>& calculate,
    csol::scenario::ConfigError& error)
{
    const auto session = runtime ? runtime->session : nullptr;
    const auto players = runtime ? runtime->players : nullptr;
    if (!session || !players || runtime->Closing() || !source.Current()) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    PlayerCall call{runtime, session, players, source, sdk, {}, {}, session->Map().RoundGeneration()};
    if (!call.OwnerCurrent()) return false;
    if (!gpGlobals || gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32 || !g_engfuncs.pfnPEntityOfEntIndex)
    { error.message = "Oberon attack client-slot lookup is unavailable"; return false; }
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        if (slot > 32) { error.message = "Oberon attack client-slot count changed"; return false; }
        auto* edict = INDEXENT(slot);
        if (!call.OwnerCurrent()) return false;
        if (!edict || edict->free) continue;
        call.target = edict;
        call.life = players->MonsterSkillLife(call.target);
        auto* player = call.Current();
        if (!player || (kind != Attack::Bomb && static_cast<int>(player->m_iTeam) == team) ||
            static_cast<std::uint32_t>(player->pev->flags) == 0x80000000u) continue;
        const bool alive = player->IsAlive() != FALSE;
        if (!call.OwnerCurrent()) return false;
        player = call.Current();
        if (!alive || !player) continue;
        const auto hit = calculate(player->pev->origin);
        if (!hit) continue;
        if (kind == Attack::Cylinder && !Impulse(call, *hit, error)) return false;
        if (!call.OwnerCurrent()) return false;
        player = call.Current();
        if (!player) continue;
        auto* inflictor = source.Current();
        auto* attacker = source.Attacker();
        if (!attacker) attacker = inflictor;
        player->TakeDamage(inflictor->pev, attacker->pev, hit->damage, kind == Attack::Bomb ? DMG_BLAST : DMG_BULLET);
        if (!call.OwnerCurrent()) return false;
        if (!call.Current()) continue;
        if (kind != Attack::Cylinder && !Impulse(call, *hit, error)) return false;
        if (!call.OwnerCurrent()) return false;
        if (kind == Attack::Cylinder && !ShockDrop(call, error)) return false;
        if (!call.OwnerCurrent()) return false;
        if (!Impact(call, kind == Attack::Cylinder, kind == Attack::Bomb, error)) return false;
        if (!call.OwnerCurrent()) return false;
    }
    return call.OwnerCurrent();
}
CSOLOberonAttackGuard Guard(CSOLMonsterSpawnCall& call)
{
    return {[](const void* value) -> CBaseEntity*
        {
            auto* current = static_cast<const CSOLMonsterSpawnCall*>(value)->Current();
            return current && current->pev->deadflag != DEAD_DEAD ? current : nullptr;
        }, &call};
}
}
bool CSOL_OberonSector(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CSOLMonsterSpawnCall& call,
    const csol::scenario::BossSkillRow& row, csol::scenario::ConfigError& error)
{
    const auto* monster = call.Current();
    if (!monster) return false;
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance) { error.message = "Oberon appearance is unknown"; return false; }
    return Apply(runtime, Guard(call), Attack::Sector, monster->pev->team,
        [&](const Vector& target)
        {
            const auto* current = call.Current();
            return csol::scenario::oberon::Sector(row, Coordinates(current->pev->origin),
                current->pev->origin.z + current->pev->mins.z, current->pev->origin.z + appearance->feetZ,
                current->pev->angles.y, Coordinates(target));
        }, error);
}
bool CSOL_OberonCylinder(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CSOLMonsterSpawnCall& call,
    const csol::scenario::BossSkillRow& row, float launchZ, bool landing, csol::scenario::ConfigError& error)
{
    const auto* monster = call.Current();
    if (!monster) return false;
    const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
    if (!appearance) { error.message = "Oberon appearance is unknown"; return false; }
    const float feet = monster->pev->origin.z + appearance->feetZ;
    const float minimum = landing ? launchZ - 196.84999084472656f : feet - 118.11000061035156f - 36.0f;
    const float maximum = landing ? launchZ + row.damageHeight : feet + row.damageHeight + 36.0f;
    return Apply(runtime, Guard(call), Attack::Cylinder, monster->pev->team,
        [&](const Vector& target)
        {
            const auto* current = call.Current();
            return csol::scenario::oberon::Cylinder(row, Coordinates(current->pev->origin), minimum,
                maximum, current->pev->origin.z + appearance->feetZ, landing, Coordinates(target));
        }, error);
}
bool CSOL_OberonBombDamage(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const CSOLOberonAttackGuard& guard, float damage, float radius,
    float kickSide, float kickUp, csol::scenario::ConfigError& error)
{
    return Apply(runtime, guard, Attack::Bomb, 0, [&](const Vector& target)
    { return csol::scenario::oberon::Bomb(damage, radius, kickSide, kickUp,
        Coordinates(guard.Current()->pev->origin), Coordinates(target)); }, error);
}
