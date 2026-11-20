// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "body_stains.h"
#include "../../scenario/custom/body_stains.hpp"
#include "../scenario/monsters/scenario_monster_pool.h"

#include <chrono>
#include <cmath>
#include <utility>

namespace
{
namespace custom = csol::scenario::custom;

struct StainedWeapon
{
    const edict_t* entity{};
    int serial{};
    string_t viewName{}, worldName{};

    bool Matches(const CBasePlayer& owner) const noexcept
    {
        if (!entity || !owner.m_pActiveItem || owner.pev->viewmodel != viewName ||
            owner.pev->weaponmodel != worldName) return false;
        const auto* active = owner.m_pActiveItem->edict();
        return active == entity && active->serialnumber == serial && !(active->v.flags & FL_KILLME);
    }
};

struct Stain
{
    int serial{};
    unsigned body{}, weapon{};
};

std::array<Stain, custom::BodyStainSlotCount> stains;
float splashRadiusSquared{};
bool enabled{}, hasWeaponStains{};
unsigned deltaTables{}, generation{};
std::uint32_t randomState{};

struct BulletTrace
{
    const CSOLBodyStainShot& shot;
    const CBaseEntity* target;
    const TraceResult* trace;
    bool recorded{};
};
BulletTrace* bulletTrace{};
struct PendingSplash
{
    const CSOLBodyStainShot* shot{};
    const CBaseEntity* victim{};
};
PendingSplash pendingSplash;

void AddHit(unsigned& mark) noexcept
{
    if (!mark)
    {
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
    }
    mark = custom::AddBodyStainHit(mark, randomState);
}

const Stain* Find(unsigned slot, const edict_t& edict) noexcept
{
    if (!slot || slot >= stains.size()) return nullptr;
    const auto& entry = stains[slot];
    return entry.serial == edict.serialnumber ? &entry : nullptr;
}

Stain* Record(CBaseEntity& actor) noexcept
{
    const auto slot = static_cast<unsigned>(actor.entindex());
    if (!slot || slot >= stains.size()) return nullptr;
    const auto serial = actor.edict()->serialnumber;
    auto& entry = stains[slot];
    if (entry.serial != serial) entry = {serial};
    return &entry;
}

unsigned WeaponMark(CBasePlayerItem& weapon) noexcept
{
    const auto* edict = weapon.edict();
    if (edict->free || (edict->v.flags & FL_KILLME)) return 0;
    const auto* entry = Find(static_cast<unsigned>(weapon.entindex()), *edict);
    return entry ? entry->weapon : 0;
}
}

void CSOL_RegisterBodyStainDelta(bool player, delta_s* schema) noexcept
{
    const unsigned bit = player ? 2u : 1u;
    if (schema && g_engfuncs.pfnDeltaFindField && DELTA_FINDFIELD(schema, custom::BodyStainDeltaField) >= 0)
        deltaTables |= bit;
    else
        deltaTables &= ~bit;
}

bool CSOL_BodyStainDeltaReady() noexcept { return deltaTables == 3; }

bool CSOL_BodyStainsActive() noexcept { return enabled; }

void CSOL_ResetBodyStains() noexcept
{
    stains.fill({});
    enabled = hasWeaponStains = false;
    randomState = 0;
    splashRadiusSquared = 0;
    pendingSplash = {};
    ++generation;
}

void CSOL_SetBodyStainPolicy(bool active, float radius) noexcept
{
    active = active && CSOL_BodyStainDeltaReady();
    const float squared = active && std::isfinite(radius) && radius > 0 ? radius * radius : 0;
    if (enabled == active && splashRadiusSquared == squared) return;
    if (!randomState && active)
        randomState = std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
    enabled = active;
    splashRadiusSquared = squared;
    pendingSplash = {};
    ++generation;
}

void CSOL_ClearBodyStain(CBaseEntity& actor) noexcept
{
    const auto* entity = actor.edict();
    if (pendingSplash.victim == &actor || (pendingSplash.shot && pendingSplash.shot->owner_ == entity))
        pendingSplash = {};
    const auto slot = static_cast<unsigned>(actor.entindex());
    if (Find(slot, *entity)) stains[slot] = {};
}

void CSOL_CopyDroppedBodyStain(CWeaponBox& box, CBasePlayerItem& weapon) noexcept
{
    if (!hasWeaponStains) return;
    if (const auto mark = WeaponMark(weapon))
        if (auto* entry = Record(box)) entry->weapon = mark;
}

CSOLBodyStainShot::CSOLBodyStainShot(CBaseEntity* shooter, const Vector& source) noexcept
{
    if (!enabled || !splashRadiusSquared || !shooter) return;
    auto* player = dynamic_cast<CBasePlayer*>(shooter);
    if (!player || player->m_iTeam != CT || !player->IsAlive() || player->has_disconnected ||
        !player->pev->viewmodel || !player->pev->weaponmodel || !player->m_pActiveItem) return;
    const auto* active = player->m_pActiveItem->edict();
    if (active->v.flags & FL_KILLME) return;
    const auto slot = player->m_pActiveItem->iItemSlot();
    if (slot != PRIMARY_WEAPON_SLOT && slot != PISTOL_SLOT) return;

    owner_ = shooter->edict();
    weapon_ = active;
    ownerSerial_ = owner_->serialnumber;
    weaponSerial_ = active->serialnumber;
    spawn_ = player->m_iNumSpawns;
    view_ = player->pev->viewmodel;
    held_ = player->pev->weaponmodel;
    source_[0] = source.x;
    source_[1] = source.y;
    source_[2] = source.z;
    generation_ = generation;
}

CSOLBodyStainShot::~CSOLBodyStainShot()
{
    if (pendingSplash.shot == this) pendingSplash = {};
}

void CSOLBodyStainShot::TraceAttack(CBaseEntity* target, entvars_t* attacker, float damage,
    const Vector& direction, TraceResult* trace, int damageType) const
{
    if (!owner_ || attacker != &owner_->v || (damageType & DMG_BULLET) == 0)
    {
        target->TraceAttack(attacker, damage, direction, trace, damageType);
        return;
    }
    BulletTrace contact{*this, target, trace};
    struct Restore
    {
        BulletTrace* previous;
        ~Restore() { bulletTrace = previous; }
    } restore{bulletTrace};
    bulletTrace = &contact;
    target->TraceAttack(attacker, damage, direction, trace, damageType);
}

void CSOL_RecordBodyStains(CCSOLScenarioMonster& monster, entvars_t* attacker,
    const TraceResult* trace, const Vector& hit) noexcept
{
    if (!enabled || !std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z)) return;
    if (auto* entry = Record(monster))
        AddHit(entry->body);

    auto* contact = bulletTrace;
    if (!contact || contact->trace != trace || contact->target != &monster || contact->recorded) return;
    contact->recorded = true;
    const auto& shot = contact->shot;
    if (shot.splashed_ || shot.generation_ != generation || attacker != &shot.owner_->v ||
        gMultiDamage.hEntity.Get<CBaseEntity>() != &monster || !(gMultiDamage.amount > 0)) return;
    const Vector distance = Vector(shot.source_[0], shot.source_[1], shot.source_[2]) - hit;
    if (DotProduct(distance, distance) <= splashRadiusSquared)
        pendingSplash = {&shot, &monster};
}

void CSOL_ClearBodyStainSplash() noexcept
{
    pendingSplash = {};
}

void CSOL_ApplyBodyStainSplash(CBaseEntity& victim, entvars_t* inflictor, entvars_t* attacker) noexcept
{
    if (!pendingSplash.shot) return;
    const auto pending = std::exchange(pendingSplash, {});
    const auto& shot = *pending.shot;
    const auto* actor = shot.owner_;
    if (!enabled || !splashRadiusSquared || shot.generation_ != generation || pending.victim != &victim ||
        inflictor != &actor->v || attacker != &actor->v || actor->free ||
        actor->serialnumber != shot.ownerSerial_ || !actor->pvPrivateData ||
        (gMultiDamage.type & DMG_BULLET) == 0 || !(gMultiDamage.amount > 0)) return;
    auto& player = *static_cast<CBasePlayer*>(actor->pvPrivateData);
    const StainedWeapon weapon{shot.weapon_, shot.weaponSerial_, shot.view_, shot.held_};
    if (player.m_iNumSpawns != shot.spawn_ || player.m_iTeam != CT || !player.IsAlive() ||
        player.has_disconnected || !weapon.Matches(player)) return;
    auto* weaponStain = Record(*player.m_pActiveItem);
    if (!weaponStain) return;
    shot.splashed_ = true;
    AddHit(weaponStain->weapon);
    hasWeaponStains = true;
    if (auto* entry = Record(player))
        AddHit(entry->body);
}

float CSOL_PackBodyStain(const edict_t& actor, unsigned index, bool player, bool local) noexcept
{
    if (!enabled) return 0;
    const auto* entry = Find(index, actor);
    if (!player)
    {
        if (!entry || !(entry->body || entry->weapon) || (actor.v.effects & EF_NODRAW)) return 0;
        return custom::EncodeBodyStainField({entry->body, entry->weapon, true});
    }

    const auto body = entry ? entry->body : 0u;
    unsigned held{};
    if (hasWeaponStains && actor.pvPrivateData && actor.v.deadflag == DEAD_NO && actor.v.health > 0)
    {
        const auto* owner = static_cast<const CBasePlayer*>(actor.pvPrivateData);
        if (owner->m_pActiveItem) held = WeaponMark(*owner->m_pActiveItem);
    }
    if (!local && !held) return 0;
    return custom::EncodeBodyStainField({local ? body : 0u, held, true});
}
