// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "smoke_zone.hpp"
#include "host_notifications.hpp"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../weapons/scenario_weapon_state.h"
#include "../scenario/monsters/scenario_monster_damage.h"
#include "../scenario/core/scenario_entity_lookup.h"
#include "../scenario/core/game_rules_gate.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

#pragma clang fp contract(off)

class CCSOLCharacterSmokeZone;
struct CharacterSmokeZoneState;

namespace
{
using csol::scenario::ConfigError;
struct SmokeRegistry
{
    std::weak_ptr<CCSOLScenarioPlayerRuntime> players;
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::uint64_t round{};
    std::array<bool, 33> inside{};
    std::vector<std::weak_ptr<CharacterSmokeZoneState>> zones;
    float nextFlush{};
    bool flushing{}, clearPending{};

    bool Current() const
    {
        const auto map = session.lock(); const auto runtime = players.lock();
        return map && runtime && runtime->Active() && access && access->Allowed() &&
            map->Map().EntityAccess() == access && map->Map().RoundGeneration() == round;
    }
};
std::weak_ptr<SmokeRegistry> smokeRegistry;
bool FlushProtection(const std::shared_ptr<SmokeRegistry>&, bool clear, ConfigError&);

struct SmokeDamageScope
{
    static thread_local SmokeDamageScope* active;
    SmokeDamageScope* previous{active};
    EHANDLE target, zone, owner;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    CCSOLMonsterPool* pool{};
    std::uint64_t lease{};
    CSOLMonsterDeathContext context;
    bool consumed{};
    SmokeDamageScope(CCSOLScenarioMonster& receiver, CBaseEntity& smoke, CBasePlayer& actor,
        std::shared_ptr<const CSOLScenarioEntityAccess> map)
        : target(&receiver), zone(&smoke), owner(&actor), access(std::move(map)),
          pool(receiver.Pool()), lease(pool ? pool->Lease(receiver) : 0)
    { context.bytes[20] = 1; active = this; }
    ~SmokeDamageScope() { active = previous; }
};
thread_local SmokeDamageScope* SmokeDamageScope::active{};

bool Integer(std::string_view text, int& value)
{
    if (text.empty()) { value = 0; return true; }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
}

struct CharacterSmokeZoneState
{
    std::shared_ptr<SmokeRegistry> registry;
    EHANDLE entity, owner;
    const CCSOLCharacterSmokeZone* identity{};
    float expires{}, nextDamage{}, damage{};
    bool spawned{}, ready{}, retired{}, busy{};
    CCSOLCharacterSmokeZone* Attached() const;
    CCSOLCharacterSmokeZone* Current() const;
    void Retire();
    void Remove();
};

class CCSOLCharacterSmokeZone final : public CBaseEntity
{
public:
    void Spawn() override;
    void OnDestroy() override;
    void SmokeThink();
    static bool Create(const std::shared_ptr<SmokeRegistry>& registry, CBasePlayer& player,
        const Vector& origin, float damage, std::shared_ptr<CharacterSmokeZoneState>& out,
        ConfigError& error);
private:
    friend struct CharacterSmokeZoneState;
    std::shared_ptr<CharacterSmokeZoneState> m_state;
};
LINK_ENTITY_TO_CLASS(smokezone, CCSOLCharacterSmokeZone, CCSEntity)

CCSOLCharacterSmokeZone* CharacterSmokeZoneState::Attached() const
{
    if (retired || !registry || !registry->access || !registry->access->Allowed()) return nullptr;
    auto* zone = dynamic_cast<CCSOLCharacterSmokeZone*>(entity.Get<CBaseEntity>());
    return zone && zone == identity && zone->m_state.get() == this && zone->pev &&
        !(zone->pev->flags & FL_KILLME) ? zone : nullptr;
}
CCSOLCharacterSmokeZone* CharacterSmokeZoneState::Current() const
{ return registry && registry->Current() ? Attached() : nullptr; }

void CharacterSmokeZoneState::Retire()
{
    if (retired) return;
    retired = true;
    if (!registry || !registry->Current()) return;
    std::erase_if(registry->zones, [](const auto& weak)
    { const auto zone = weak.lock(); return !zone || !zone->Current(); });
    // The final smoke removes source8 immediately. A surviving smoke owns
    // the shared 0.2-second aggregation, including overlapping zones.
    if (registry->zones.empty())
    {
        registry->inside = {};
        registry->clearPending = true;
        ConfigError error;
        FlushProtection(registry, true, error);
    }
}
void CharacterSmokeZoneState::Remove()
{
    auto* zone = Attached();
    const EHANDLE handle(zone);
    Retire();
    if (!zone || !registry->access->Allowed() || handle.Get<CBaseEntity>() != zone) return;
    zone->SetThink(nullptr); zone->pev->nextthink = 0;
    UTIL_Remove(zone);
}

namespace
{
bool FlushProtection(const std::shared_ptr<SmokeRegistry>& registry, bool clear, ConfigError& error)
{
    if (!registry->Current() || registry->flushing || !gpGlobals) return false;
    registry->flushing = true;
    struct Guard { bool& busy; ~Guard() { busy = false; } } guard{registry->flushing};
    const auto players = registry->players.lock();
    const auto inside = registry->inside;
    clear = clear || registry->clearPending;
    registry->clearPending = false;
    registry->inside = {};
    const float now = gpGlobals->time;
    for (int slot = 1; slot <= gpGlobals->maxClients && slot <= 32; ++slot)
    {
        auto* entity = CSOL_ScenarioPlayerEntity(slot, registry->access);
        if (!registry->Current()) return false;
        auto* player = dynamic_cast<CBasePlayer*>(entity);
        if (!player || !player->pev || player->has_disconnected ||
            (player->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        const EHANDLE handle(player);
        const auto state = players->Find(handle);
        if (!state) continue;
        const auto life = state->skillLifeSerial;
        const auto current = [&]
        { return registry->Current() && handle.Get<CBasePlayer>() == player &&
            players->Find(handle) == state && state->skillLifeSerial == life; };
        std::vector<std::uint64_t> previous;
        float remaining{};
        for (const auto& buff : state->buffs)
        {
            if (buff.source != 8) continue;
            previous.push_back(buff.token);
            if (buff.Active(now)) remaining = std::max(remaining,
                buff.expires <= 0 ? std::numeric_limits<float>::max() : buff.expires - now);
        }
        const bool protect = !clear && inside[slot];
        if (protect && remaining >= 1) continue;
        for (const auto token : previous)
        {
            if (!current()) break;
            if (!players->RemoveStatBuff(handle, token) && current())
            { error.message = "smoke protection removal failed"; return false; }
        }
        if (!registry->Current()) return false;
        if (!protect || !current()) continue;
        CSOLScenarioStatBuff buff;
        buff.group = 3; buff.source = 8; buff.protection = true; buff.mask = 0;
        buff.expires = now + 5;
        const auto token = players->InstallStatBuff(handle, std::move(buff));
        if (!registry->Current()) return false;
        if (!current()) continue;
        if (!token || !players->HoldSkillProtectionRender(handle, token, error)) return false;
    }
    if (registry->Current() && registry->clearPending)
    {
        registry->flushing = false;
        return FlushProtection(registry, true, error);
    }
    return registry->Current();
}

bool SmokeDamage(const std::shared_ptr<CharacterSmokeZoneState>& state, bool impulse, ConfigError& error)
{
    auto* smoke = state->Current();
    if (!smoke || !gpGlobals) return false;
    auto* actor = state->owner.Get<CBasePlayer>();
    if (!actor || !actor->pev || actor->has_disconnected ||
        (actor->pev->flags & (FL_KILLME | FL_DORMANT))) return true;
    if (!g_engfuncs.pfnFindEntityInSphere || !g_engfuncs.pfnEntOffsetOfPEntity) return false;
    const int ownerOffset = OFFSET(actor->edict());
    if (!state->Current()) return false;
    if (!ownerOffset || state->owner.Get<CBasePlayer>() != actor) return true;
    const Vector origin = smoke->pev->origin;
    const int team = smoke->pev->team;
    const auto ownerCurrent = [&]
    { return state->Current() && state->owner.Get<CBasePlayer>() == actor && actor->pev &&
        !actor->has_disconnected && !(actor->pev->flags & (FL_KILLME | FL_DORMANT)); };
    edict_t* cursor{};
    while (ownerCurrent())
    {
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, 3.0f * 39.37f);
        if (!ownerCurrent()) return state->Current() != nullptr;
        if (FNullEnt(cursor)) return true;
        auto* target = CBaseEntity::Instance(cursor);
        if (!target || !target->pev || (target->pev->flags & (FL_KILLME | FL_DORMANT))) continue;
        const EHANDLE handle(target);
        auto* monster = dynamic_cast<CCSOLScenarioMonster*>(target);
        auto* pool = monster ? monster->Pool() : nullptr;
        const auto lease = pool ? pool->Lease(*monster) : 0;
        const auto current = [&]
        { return ownerCurrent() && handle.Get<CBaseEntity>() == target && target->pev &&
            !(target->pev->flags & (FL_KILLME | FL_DORMANT)) &&
            (!monster || (pool && monster->Pool() == pool && pool->Lease(*monster) == lease)); };
        const bool alive = target->IsAlive();
        if (!ownerCurrent()) return state->Current() != nullptr;
        if (!current() || !alive || target->pev->takedamage == DAMAGE_NO) continue;
        const bool isPlayer = target->IsPlayer();
        if (!ownerCurrent()) return state->Current() != nullptr;
        if (!current() || isPlayer || (target->pev->team == team && friendlyfire.value == 0)) continue;
        if (monster)
        {
            SmokeDamageScope dispatch(*monster, *state->Current(), *actor, state->registry->access);
            target->TakeDamage(state->Current()->pev, actor->pev, state->damage, 0);
        }
        else target->TakeDamage(state->Current()->pev, actor->pev, state->damage, 0);
        if (!ownerCurrent()) return state->Current() != nullptr;
        if (!impulse || !current() || target->pev->deadflag != DEAD_NO || target->pev->health <= 0) continue;
        const int classification = target->Classify();
        if (!ownerCurrent()) return state->Current() != nullptr;
        if (!current()) continue;
        const auto* type = monster ? monster->Type() : nullptr;
        const bool movable = classification == 45 || (classification == 17 && type &&
            type->skill < 1000 && type->eventClass <= 149);
        if (!movable || !monster || !pool->Borrowed(*monster)) continue;
        const Vector delta = target->pev->origin - origin;
        const float squared = delta.x * delta.x + delta.y * delta.y;
        if (!std::isfinite(squared) || squared < 0)
        { error.message = "smoke knockback direction is invalid"; return false; }
        const float length = static_cast<float>(std::sqrt(static_cast<double>(squared)));
        const float inverse = length == 0 ? 0 : 1 / length;
        if (!monster->HostState().knockbackBlocked)
        {
            monster->SpeedState().impulse = Vector2D((delta.x * inverse) * 300, (delta.y * inverse) * 300);
            monster->pev->velocity.z = 100;
            monster->SpeedState().impulseUntil = gpGlobals->time + .2f;
        }
        if (classification == 17) monster->HostState().moveAfter = gpGlobals->time + 1;
    }
    return state->Current() != nullptr;
}

bool SmokeStrength(CCSOLScenarioSession& session, CBasePlayer& player, float& damage, ConfigError& error)
{
    damage = 0;
    // Original106E3060 reads the owned knife slot and selected knife item,
    // independently of the currently deployed primary/secondary weapon.
    if (!player.m_rgpPlayerItems[KNIFE_SLOT]) return true;
    const auto weapons = session.Weapons();
    const auto catalog = weapons ? weapons->ItemCatalog() : nullptr;
    if (!catalog) { error.message = "smoke knife item catalog is unavailable"; return false; }
    std::int32_t item{};
    if (!weapons->Item(player, WEAPON_KNIFE, item, error)) return false;
    int grade{}; bool found{};
    for (const auto& row : catalog->records)
    {
        int id{};
        if (row.fields.empty() || !Integer(row.fields.front(), id) || id != item) continue;
        if (row.fields.size() <= 47 || !Integer(row.fields[47], grade))
        { error.message = "smoke knife ItemGrade is invalid"; return false; }
        found = true; break;
    }
    if (!found) { error.message = "smoke knife item is absent from the loaded catalog"; return false; }
    constexpr float difficulty[] = {1, 1.25f, 1.5f, 2, 2.25f, 3, 3.5f};
    constexpr float grades[] = {1, 1.25f, 1.5f, 2.5f, 4, 4.5f, 0, 4.25f};
    const int row = session.Difficulty().SelectedRow();
    const float base = 400 * (row >= 1 && row <= 7 ? difficulty[row - 1] : 3.5f);
    damage = base * (grade >= 1 && grade <= 8 ? grades[grade - 1] : 1);
    return std::isfinite(damage);
}
}

void CCSOLCharacterSmokeZone::Spawn()
{
    const auto state = m_state;
    if (!state || !state->Current() || !gpGlobals) return;
    pev->movetype = MOVETYPE_NONE; pev->solid = SOLID_NOT; pev->takedamage = DAMAGE_NO;
    pev->effects |= EF_NODRAW;
    const Vector extent(2.5f * 39.37f, 2.5f * 39.37f, 2.5f * 39.37f);
    UTIL_SetSize(pev, -extent, extent);
    if (!state->Current()) return;
    state->expires = gpGlobals->time + 4.2f; state->nextDamage = gpGlobals->time + 1;
    if (gpGlobals->time - state->registry->nextFlush < -1) state->registry->nextFlush = 0;
    state->spawned = true;
    SetThink(&CCSOLCharacterSmokeZone::SmokeThink); pev->nextthink = gpGlobals->time;
}

void CCSOLCharacterSmokeZone::SmokeThink()
{
    const auto state = m_state;
    if (!state || state->busy || state->retired) return;
    const auto session = state->registry->session.lock();
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!state->ready || !gpGlobals) return;
    state->busy = true;
    struct Guard { bool& busy; ~Guard() { busy = false; } } guard{state->busy};
    const float now = gpGlobals->time;
    if (!std::isfinite(now)) { state->Remove(); return; }
    pev->nextthink = now + .1f;
    ConfigError error;
    if (now > state->nextDamage)
    {
        state->nextDamage = now + 1;
        if (!SmokeDamage(state, false, error) || !sdk.Current() || !state->Current())
        { state->Remove(); return; }
    }
    const auto registry = state->registry;
    const auto minimum = state->Current()->pev->absmin;
    const auto maximum = state->Current()->pev->absmax;
    for (int slot = 1; slot <= gpGlobals->maxClients && slot <= 32; ++slot)
    {
        auto* target = CSOL_ScenarioPlayerEntity(slot, registry->access);
        if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
        if (!target || !target->pev || !dynamic_cast<CBasePlayer*>(target)) continue;
        const Vector p = target->pev->origin;
        if (p.x >= minimum.x && p.x <= maximum.x && p.y >= minimum.y && p.y <= maximum.y &&
            p.z >= minimum.z && p.z <= maximum.z) registry->inside[slot] = true;
    }
    if (now > registry->nextFlush)
    {
        registry->nextFlush = now + .2f;
        if (!FlushProtection(registry, false, error) || !sdk.Current() || !state->Current())
        { state->Remove(); return; }
    }
    if (now > state->expires) state->Remove();
}

void CCSOLCharacterSmokeZone::OnDestroy()
{
    const auto state = std::move(m_state);
    if (state) state->Retire();
    CBaseEntity::OnDestroy();
}

bool CCSOLCharacterSmokeZone::Create(const std::shared_ptr<SmokeRegistry>& registry, CBasePlayer& player,
    const Vector& origin, float damage, std::shared_ptr<CharacterSmokeZoneState>& out, ConfigError& error)
{
    if (!registry->Current() || !g_engfuncs.pfnCreateNamedEntity || !g_engfuncs.pfnAllocString || !gpGlobals) return false;
    const EHANDLE owner(&player);
    const int team = static_cast<int>(player.m_iTeam);
    const auto name = ALLOC_STRING("smokezone");
    if (!registry->Current() || owner.Get<CBasePlayer>() != &player) return false;
    auto* edict = CREATE_NAMED_ENTITY(name);
    if (!registry->access->Allowed()) return false;
    const EHANDLE allocated(edict);
    auto* entity = dynamic_cast<CCSOLCharacterSmokeZone*>(allocated.Get<CBaseEntity>());
    if (!registry->Current() || owner.Get<CBasePlayer>() != &player)
    { if (entity && !entity->m_state) UTIL_Remove(entity); return false; }
    if (!entity || entity->m_state)
    { error.message = "smoke-zone entity allocation failed"; return false; }
    const auto state = std::make_shared<CharacterSmokeZoneState>();
    state->registry = registry; state->entity = entity; state->identity = entity;
    state->owner = owner; state->damage = damage;
    entity->m_state = state;
    registry->zones.push_back(state);
    entity->pev->owner = owner.Get(); entity->pev->team = team;
    UTIL_SetOrigin(entity->pev, origin);
    if (!state->Current()) { state->Remove(); return false; }
    const int dispatched = DispatchSpawn(edict);
    if (!state->Current() || dispatched < 0 || !state->spawned)
    { state->Remove(); error.message = "smoke-zone Spawn failed"; return false; }
    state->ready = true; out = state;
    return true;
}

std::optional<CSOLMonsterDeathContext> CSOL_ConsumeSmokeSkillDamageContext(
    CCSOLScenarioMonster& target, entvars_t* inflictor, entvars_t* attacker, int bits)
{
    auto* scope = SmokeDamageScope::active;
    if (!scope || scope->consumed || bits != 0 || !scope->access || !scope->access->Allowed() ||
        scope->target.Get<CBaseEntity>() != &target || !scope->pool || target.Pool() != scope->pool ||
        scope->pool->Lease(target) != scope->lease) return {};
    auto* zone = scope->zone.Get<CBaseEntity>(); auto* owner = scope->owner.Get<CBasePlayer>();
    if (!zone || !owner || zone->pev != inflictor || owner->pev != attacker) return {};
    scope->consumed = true;
    return scope->context;
}

namespace csol::scenario::skills
{
bool StartSmokeCloakSkill(int subtype, const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, std::uint64_t identity, ConfigError& error)
{
    if (!players || !state || !session || !identity || (subtype != 22 && subtype != 23) || !gpGlobals ||
        !std::isfinite(gpGlobals->time)) return false;
    const auto access = session->Map().EntityAccess();
    const auto life = state->skillLifeSerial; const auto skill = state->activeSkill;
    const auto current = [&]
    { return access && access->Allowed() && session->Map().EntityAccess() == access && players->Active() &&
        players->Find(state->player) == state && state->skillLifeSerial == life && state->activeSkill == skill; };
    if (!current()) return false;
    auto* actor = state->player.Get<CBasePlayer>();
    if (!actor || !actor->pev || actor->has_disconnected || (actor->pev->flags & (FL_KILLME | FL_DORMANT))) return false;
    const Vector origin = actor->pev->origin;
    if (!PublishSmokeSkillStart(*session, state->player, {origin.x, origin.y, origin.z}, error) || !current()) return false;
    float damage{};
    if (!SmokeStrength(*session, *actor, damage, error) || !current()) return false;
    auto registry = smokeRegistry.lock();
    if (!registry || !registry->Current() || registry->session.lock() != session)
    {
        registry = std::make_shared<SmokeRegistry>(); registry->players = players; registry->session = session;
        registry->access = access; registry->round = session->Map().RoundGeneration();
        smokeRegistry = registry;
    }
    std::shared_ptr<CharacterSmokeZoneState> zone;
    if (!CCSOLCharacterSmokeZone::Create(registry, *actor, origin, damage, zone, error) || !current()) return false;
    if (!SmokeDamage(zone, true, error) || !current()) { zone->Remove(); return false; }
    if (!PublishSmokeSkillWave(*session, {origin.x, origin.y, origin.z}, error) || !current()) return false;
    return players->StartCloakSkill(state->player, 11, identity, error, subtype) && current();
}
}
