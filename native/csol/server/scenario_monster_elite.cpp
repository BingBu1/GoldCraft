// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_elite.h"
#include "scenario_character_skills/elite_manager.hpp"
#include "scenario_character_skills/host_notifications.hpp"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_player_runtime.h"
#include "scenario_entity_lookup.h"
#include "scenario_monster_damage.h"
#include "game_rules_gate.h"
#include "../scenario/monster_appearances.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

#pragma clang fp contract(off)

namespace
{
using csol::scenario::ConfigError;
bool Fail(ConfigError& error, const char* message) { error.message = message; return false; }
bool LivePlayer(CBasePlayer* player)
{
    return player && player->pev && !player->has_disconnected &&
        !(player->pev->flags & (FL_KILLME | FL_DORMANT)) &&
        player->pev->deadflag == DEAD_NO && player->pev->health > 0;
}
}

class CCSOLEliteAbilityEntity;
struct CSOLEliteAbilityEntityState
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::shared_ptr<const csol::scenario::EliteSettings> settings;
    CSOLEliteVisualPublisher visual;
    EHANDLE entity, owner, target;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> targetLife;
    const CCSOLEliteAbilityEntity* identity{};
    CCSOLMonsterPool* ownerPool{};
    std::uint64_t round{}, ownerLease{};
    int ability{}, speed{}, phase{};
    Vector axis{};
    float damage{}, duration{}, until{};
    bool busy{}, retired{}, ready{};
    CCSOLEliteAbilityEntity* Attached() const;
    CCSOLEliteAbilityEntity* Current() const;
    bool OwnerCurrent() const;
    float Parameter(const char* name) const
    { return settings->Parameter(ability, session->Difficulty().SelectedRow(), name); }
    void Remove();
};

class CCSOLEliteAbilityEntity final : public CBaseAnimating
{
public:
    void Spawn() override {} // Factory assigns the exact original fields.
    void OnDestroy() override;
    void AbilityThink();
    void FireballTouch(CBaseEntity*);
    void Killed(entvars_t*, int) override;
    static bool Create(CSOLMonsterSpawnCall&, std::shared_ptr<CCSOLScenarioRuntime>,
        std::shared_ptr<const csol::scenario::EliteSettings>, CSOLEliteVisualPublisher,
        int ability, float damage, const Vector& origin, const Vector& axis,
        csol::scenario::ConfigError&);
private:
    friend struct CSOLEliteAbilityEntityState;
    bool DamagePlayers(const std::shared_ptr<CSOLEliteAbilityEntityState>&, bool wall,
        csol::scenario::ConfigError&, float radius = 39.37f);
    void Explode(const std::shared_ptr<CSOLEliteAbilityEntityState>&);
    std::shared_ptr<CSOLEliteAbilityEntityState> m_state;
};
LINK_ENTITY_TO_CLASS(elitefireball, CCSOLEliteAbilityEntity, CCSEntity)
LINK_ENTITY_TO_CLASS(elitefirewall, CCSOLEliteAbilityEntity, CCSEntity)
LINK_ENTITY_TO_CLASS(elitethunder, CCSOLEliteAbilityEntity, CCSEntity)

CCSOLEliteAbilityEntity* CSOLEliteAbilityEntityState::Attached() const
{
    if (retired || !access || !access->Allowed()) return nullptr;
    auto* value = dynamic_cast<CCSOLEliteAbilityEntity*>(entity.Get<CBaseEntity>());
    return value && value == identity && value->pev && value->m_state.get() == this &&
        !(value->pev->flags & FL_KILLME) ? value : nullptr;
}
CCSOLEliteAbilityEntity* CSOLEliteAbilityEntityState::Current() const
{
    const auto root = runtime.lock();
    return root && !root->Closing() && root->session == session && session &&
        session->Map().EntityAccess() == access && session->Map().RoundGeneration() == round ? Attached() : nullptr;
}
bool CSOLEliteAbilityEntityState::OwnerCurrent() const
{
    auto* value = owner.Get<CCSOLScenarioMonster>();
    return value && value->pev && !(value->pev->flags & (FL_KILLME | FL_DORMANT)) &&
        ownerPool && value->Pool() == ownerPool && ownerPool->Lease(*value) == ownerLease;
}
void CSOLEliteAbilityEntityState::Remove()
{
    if (auto* value = Attached())
    {
        retired = true;
        value->SetThink(nullptr);
        value->SetTouch(nullptr);
        value->pev->nextthink = 0;
        UTIL_Remove(value);
    }
}
void CCSOLEliteAbilityEntity::OnDestroy()
{
    if (m_state) m_state->retired = true;
    m_state.reset();
    CBaseAnimating::OnDestroy();
}
void CCSOLEliteAbilityEntity::Killed(entvars_t*, int)
{
    // Destroying the projectile uses the base removal path. Its explosive
    // Think/Touch callbacks are distinct from being shot down.
    if (const auto state = m_state)
        if (state->Current() == this)
        {
            pev->takedamage = DAMAGE_NO;
            pev->deadflag = DEAD_DEAD;
            state->Remove();
        }
}

namespace
{
struct EliteEntityScope
{
    std::shared_ptr<CSOLEliteAbilityEntityState> state;
    explicit EliteEntityScope(std::shared_ptr<CSOLEliteAbilityEntityState> value) : state(std::move(value))
    { state->busy = true; }
    ~EliteEntityScope() { state->busy = false; }
};
}

bool CCSOLEliteAbilityEntity::DamagePlayers(const std::shared_ptr<CSOLEliteAbilityEntityState>& state,
    bool wall, ConfigError& error, float radius)
{
    auto* current = state->Current();
    if (!current || !gpGlobals) return false;
    const Vector center = current->pev->origin;
    if (!std::isfinite(radius) || radius < 0) return Fail(error, "elite entity radius is invalid");
    const Vector extent(state->axis.x * 59.055f + state->axis.y * 19.685f,
        state->axis.y * 59.055f - state->axis.x * 19.685f, 0);
    const Vector lo(center.x - std::abs(extent.x), center.y - std::abs(extent.y), center.z);
    const Vector hi(center.x + std::abs(extent.x), center.y + std::abs(extent.y), center.z + 100);
    // Keep the engine's edict cursor, which remains an index in this map
    // even when a damage callback deletes its private data. Resolving a
    // removed EHANDLE to null would restart the sphere at the first player.
    edict_t* cursor{};
    int index = 0;
    for (;;)
    {
        CBaseEntity* next{};
        if (wall)
        {
            if (++index > gpGlobals->maxClients) break;
            next = CSOL_ScenarioPlayerEntity(index, state->access);
        }
        else
        {
            cursor = FIND_ENTITY_IN_SPHERE(cursor, center, radius);
            if (!state->Current()) return false;
            const bool exhausted = FNullEnt(cursor);
            if (!state->Current()) return false;
            if (exhausted) break;
            if (cursor->free) continue;
            next = CBaseEntity::Instance(cursor);
        }
        current = state->Current();
        if (!current) return false;
        if (!next) continue;
        auto* player = dynamic_cast<CBasePlayer*>(next);
        if (!LivePlayer(player) || (!wall && player->pev->takedamage == DAMAGE_NO)) continue;
        if (wall && (lo.x > player->pev->absmax.x || lo.y > player->pev->absmax.y ||
            lo.z > player->pev->absmax.z || player->pev->absmin.x > hi.x ||
            player->pev->absmin.y > hi.y || player->pev->absmin.z > hi.z)) continue;
        const float damage = state->ability == 3 ? state->damage : state->Parameter("Damage");
        if (!std::isfinite(damage)) return Fail(error, "elite entity damage is invalid");
        // The auxiliary entity, not its creator, is the raw damage source.
        player->TakeDamage(current->pev, current->pev, damage, 0);
        if (!state->Current()) return false;
    }
    return true;
}

void CCSOLEliteAbilityEntity::Explode(const std::shared_ptr<CSOLEliteAbilityEntityState>& state)
{
    ConfigError error;
    if (state->OwnerCurrent())
    {
        const float radius = state->Parameter("Range") * 39.37f;
        if (!DamagePlayers(state, false, error, radius))
        {
            state->Remove();
            if (!error.message.empty()) CSOL_RejectGameRules(error.message.c_str());
            return;
        }
        if (state->Current())
        {
            const bool sent = state->visual(*state->session, state->entity, CSOLEliteEntityVisual::FireballExplosion,
                g_vecZero, 0, radius, error);
            if (!sent && state->Current() && error.message.empty())
                Fail(error, "elite fireball explosion publisher failed");
        }
    }
    state->Remove();
    if (!error.message.empty()) CSOL_RejectGameRules(error.message.c_str());
}

void CCSOLEliteAbilityEntity::FireballTouch(CBaseEntity* other)
{
    const auto state = m_state;
    if (!state || state->busy || !state->ready || !other) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!other->pev || (other->pev->flags & FL_KILLME)) return;
    const EHANDLE target(other);
    const bool player = other->IsPlayer() != FALSE;
    if (!state->Current() || target.Get<CBaseEntity>() != other) return;
    const bool world = !player && other->IsBSPModel() != FALSE;
    if (!state->Current()) return;
    if (player || world) { EliteEntityScope busy(state); Explode(state); }
}

void CCSOLEliteAbilityEntity::AbilityThink()
{
    const auto state = m_state;
    if (!state || state->busy || !state->ready) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    EliteEntityScope busy(state);
    const auto runtime = state->runtime.lock();
    if (!gpGlobals || !runtime || !runtime->eliteManager) { state->Remove(); return; }
    if (runtime->eliteManager->Disabled()) state->until = 0;
    ConfigError error;
    if (state->ability == 3)
    {
        auto* target = state->target.Get<CBasePlayer>();
        if (!LivePlayer(target) || !runtime->players ||
            runtime->players->MonsterSkillLife(state->target) != state->targetLife ||
            gpGlobals->time > state->until) { Explode(state); return; }
        const Vector delta = target->pev->origin - pev->origin;
        const float length = static_cast<float>(std::sqrt(static_cast<double>(
            (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z)));
        const float inverse = length == 0 ? 0 : 1.0f / length;
        pev->velocity = delta * inverse * static_cast<float>(state->speed);
        pev->nextthink = gpGlobals->time + 0.1f;
        return;
    }
    if (state->ability == 7)
    {
        if (state->phase++ == 0)
        {
            const bool applied = DamagePlayers(state, false, error);
            if (state->Current() && applied) pev->nextthink = gpGlobals->time + 0.6f;
            else state->Remove();
        }
        else state->Remove();
    }
    else if (state->phase < 4)
    {
        const float offsets[] = {29.5275f, -29.5275f, 59.055f, -59.055f};
        const auto visual = state->phase % 2 == 0 ? CSOLEliteEntityVisual::FirewallPositiveSegment :
            CSOLEliteEntityVisual::FirewallNegativeSegment;
        const Vector offset = state->axis * offsets[state->phase++];
        const bool sent = state->visual(*state->session, state->entity,
            visual, offset, state->duration, 0, error);
        if (!state->Current() || !sent) state->Remove();
        else if (state->phase == 4)
        {
            pev->nextthink = gpGlobals->time + 0.5f;
            if (!DamagePlayers(state, true, error)) state->Remove();
        }
        else pev->nextthink = gpGlobals->time + 0.1f;
    }
    else if (gpGlobals->time > state->until) state->Remove();
    else
    {
        pev->nextthink = gpGlobals->time + 0.5f;
        if (!DamagePlayers(state, true, error)) state->Remove();
    }
    if (!error.message.empty()) CSOL_RejectGameRules(error.message.c_str());
}

bool CCSOLEliteAbilityEntity::Create(CSOLMonsterSpawnCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime,
    std::shared_ptr<const csol::scenario::EliteSettings> settings, CSOLEliteVisualPublisher visual,
    int ability, float damage, const Vector& origin, const Vector& axis,
    ConfigError& error)
{
    const auto session = runtime ? runtime->session : nullptr;
    if (!session || !settings || !call.Current() || !call.BelongsTo(session->Map())) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    const auto valid = [&] { return sdk.Current() && !runtime->Closing() && runtime->session == session &&
        call.Current() && call.Current()->pev->deadflag == DEAD_NO && call.Current()->pev->health > 0; };
    if (!valid() || !gpGlobals || !g_engfuncs.pfnCreateNamedEntity || !g_engfuncs.pfnAllocString ||
        !g_engfuncs.pfnSetModel || !g_engfuncs.pfnSetSize || !g_engfuncs.pfnSetOrigin ||
        !g_engfuncs.pfnFindEntityInSphere || !g_engfuncs.pfnGetModelPtr ||
        !g_engfuncs.pfnModelIndex || !g_engfuncs.pfnEntOffsetOfPEntity ||
        !g_engfuncs.pfnRandomLong || !g_engfuncs.pfnPEntityOfEntIndex ||
        (ability != 3 && ability != 6 && ability != 7))
        return Fail(error, "elite auxiliary entity host is unavailable");
    if (ability != 7 && !visual) return Fail(error, "elite auxiliary visual publisher is unavailable");
    if ((ability == 6 || ability == 7) && !g_engfuncs.pfnDropToFloor)
        return Fail(error, "elite auxiliary entity requires engine drop-to-floor");
    const char* name = ability == 3 ? "elitefireball" : ability == 6 ? "elitefirewall" : "elitethunder";
    const char* model = ability == 3 ? "models/ZombieGiant/revenant_fireball.mdl" :
        ability == 6 ? "models/null.mdl" : "models/elite_thunder.mdl";
    const int modelIndex = MODEL_INDEX(model);
    if (!valid()) return false;
    if (modelIndex <= 0) return Fail(error, "elite auxiliary model was not precached");
    const auto string = ALLOC_STRING(name);
    if (!valid()) return false;
    const auto access = session->Map().EntityAccess();
    edict_t* edict = CREATE_NAMED_ENTITY(string);
    if (!access->Allowed()) return false;
    const EHANDLE allocated(edict);
    auto* entity = dynamic_cast<CCSOLEliteAbilityEntity*>(allocated.Get<CBaseEntity>());
    if (!valid())
    { if (entity && !entity->m_state) UTIL_Remove(entity); return false; }
    if (!edict || edict->free) return true; // Original allocation failure consumes the clock.
    if (!entity || entity->m_state) return Fail(error, "elite auxiliary private data is invalid");
    const auto state = std::make_shared<CSOLEliteAbilityEntityState>();
    state->runtime = runtime;
    state->session = session;
    state->settings = std::move(settings);
    state->visual = std::move(visual);
    state->access = access;
    state->round = session->Map().RoundGeneration();
    state->entity = entity;
    state->identity = entity;
    state->owner = call.Current();
    state->ownerPool = call.Current()->Pool();
    state->ownerLease = state->ownerPool->Lease(*call.Current());
    state->ability = ability;
    state->damage = damage;
    state->axis = axis;
    state->duration = state->Parameter("Duration");
    state->until = gpGlobals->time + state->duration;
    if (!std::isfinite(state->duration) || !std::isfinite(state->until))
    { UTIL_Remove(entity); return Fail(error, "elite auxiliary duration is invalid"); }
    entity->m_state = state;
    const auto current = [&]() -> CCSOLEliteAbilityEntity*
    {
        if (valid()) if (auto* value = state->Current()) return value;
        state->Remove();
        return nullptr;
    };
    entity->pev->angles = g_vecZero;
    UTIL_SetOrigin(entity->pev, origin);
    if (!(entity = current())) return false;
    if (DispatchSpawn(entity->edict()) < 0)
    { state->Remove(); return Fail(error, "elite auxiliary Spawn was rejected"); }
    if (!(entity = current())) return false;
    SET_MODEL(entity->edict(), model);
    if (!(entity = current())) return false;
    entity->pev->frame = 0;
    entity->pev->sequence = 0;
    if (ability != 6)
    {
        const bool reset = CSOL_ResetScenarioSequenceInfo(*entity,
            [](const void* value) -> CBaseAnimating*
            { return static_cast<const CSOLEliteAbilityEntityState*>(value)->Current(); }, state.get());
        if (!(entity = current()) || !reset) { state->Remove(); return false; }
    }
    if (ability == 3)
    {
        const auto speed = std::trunc(static_cast<double>(state->Parameter("Speed")));
        const float health = state->Parameter("HP");
        if (!std::isfinite(speed) || speed < INT32_MIN || speed > INT32_MAX || !std::isfinite(health))
        { state->Remove(); return Fail(error, "elite fireball fields are invalid"); }
        state->speed = static_cast<int>(speed);
        state->until = gpGlobals->time + 10.0f;
        entity->pev->movetype = MOVETYPE_NOCLIP;
        entity->pev->solid = SOLID_BBOX;
        entity->pev->takedamage = DAMAGE_YES;
        entity->pev->health = entity->pev->max_health = health;
        entity->pev->rendermode = kRenderTransAdd;
        entity->pev->renderfx = kRenderFxNone;
        entity->pev->renderamt = 230;
        //1096801E copies the newly created entity's owner/world team, not
        // its elite creator. The no-owner host entity stays neutral.
        UTIL_SetSize(entity->pev, Vector(-40,-40,-40), Vector(40,40,40));
        if (!(entity = current())) return false;
        //109680A8 selects a random living player only AFTER successful
        // allocation/model/bounds setup. Failed allocation consumes no RNG.
        if (!runtime->players || !runtime->players->Active() ||
            gpGlobals->maxClients < 0 || gpGlobals->maxClients > 32)
        { state->Remove(); return Fail(error, "elite fireball target registry is unavailable"); }
        struct Candidate
        {
            EHANDLE handle;
            std::optional<std::pair<std::uint64_t, std::uint64_t>> life;
        };
        std::vector<Candidate> candidates;
        for (int index = 1; index <= gpGlobals->maxClients; ++index)
        {
            auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, access));
            if (!(entity = current())) return false;
            if (!LivePlayer(player)) continue;
            const EHANDLE handle(player);
            candidates.push_back({handle, runtime->players->MonsterSkillLife(handle)});
        }
        if (!candidates.empty())
        {
            const int selected = RANDOM_LONG(0, static_cast<int>(candidates.size()) - 1);
            if (!(entity = current())) return false;
            if (selected < 0 || static_cast<std::size_t>(selected) >= candidates.size())
            { state->Remove(); return Fail(error, "elite fireball random source is out of range"); }
            const auto& candidate = candidates[selected];
            if (runtime->players->MonsterSkillLife(candidate.handle) == candidate.life)
            { state->target = candidate.handle; state->targetLife = candidate.life; }
        }
        entity->SetTouch(&CCSOLEliteAbilityEntity::FireballTouch);
        const bool sent = state->visual(*session, state->entity, CSOLEliteEntityVisual::FireballTrail,
            g_vecZero, 10, 0, error);
        if (!(entity = current()) || !sent) { state->Remove(); return false; }
        entity->pev->nextthink = gpGlobals->time + 0.1f;
    }
    else
    {
        if (ability == 7)
        {
            Vector lo, hi;
            if (!entity->ExtractBbox(0, lo, hi))
            { state->Remove(); return Fail(error, "elite thunder sequence bounds are unavailable"); }
            if (!(entity = current())) return false;
            UTIL_SetSize(entity->pev, lo * 0.5f, hi * 0.5f);
            if (!(entity = current())) return false;
            entity->pev->nextthink = gpGlobals->time + state->Parameter("Interval");
        }
        DROP_TO_FLOOR(entity->edict());
        if (!(entity = current())) return false;
        if (ability == 6)
        {
            // Initial center burn7 precedes four lateral segments. Axis is
            // derived from the unnormalized perpendicular target direction.
            const bool sent = state->visual(*session, state->entity, CSOLEliteEntityVisual::FirewallSegment,
                g_vecZero, state->duration, 0, error);
            if (!(entity = current()) || !sent) { state->Remove(); return false; }
            if (!entity->DamagePlayers(state, true, error)) { state->Remove(); return false; }
            if (!(entity = current())) return false;
            entity->pev->nextthink = gpGlobals->time + 0.1f;
        }
    }
    entity->SetThink(&CCSOLEliteAbilityEntity::AbilityThink);
    state->ready = true;
    return current() != nullptr;
}

CCSOLEliteMonsterBehavior::CCSOLEliteMonsterBehavior(
    std::shared_ptr<CCSOLOrdinaryMonsterServices> services,
    std::shared_ptr<CCSOLOrdinaryMonsterDamage> damage,
    std::weak_ptr<CCSOLScenarioRuntime> runtime,
    std::shared_ptr<const csol::scenario::EliteSettings> settings,
    CSOLEliteMonsterBindings bindings)
    : CCSOLOrdinaryMonsterBehavior(std::move(services), std::move(damage)),
      m_runtime(std::move(runtime)), m_settings(std::move(settings)), m_bindings(std::move(bindings)) {}

std::shared_ptr<CCSOLScenarioRuntime> CCSOLEliteMonsterBehavior::Pin(
    CSOLMonsterSpawnCall& call, const EliteStatePtr& state) const
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || !runtime->session || !runtime->eliteManager ||
        !runtime->eliteManager->Current() || !state || state != m_elite ||
        !CurrentAlive(call, state->ordinary) || !call.BelongsTo(runtime->session->Map())) return {};
    return runtime;
}

float CCSOLEliteMonsterBehavior::Parameter(int ability, const char* name) const
{
    const auto runtime = m_runtime.lock();
    return runtime && runtime->session && m_settings ?
        m_settings->Parameter(ability, runtime->session->Difficulty().SelectedRow(), name) : 0;
}

bool CCSOLEliteMonsterBehavior::Follower() const
{ return m_elite && m_elite->follower; }

bool CCSOLEliteMonsterBehavior::AbilityActive(int ability, float now) const
{
    if (!m_elite) return false;
    const auto found = m_elite->abilities.find(ability);
    return found != m_elite->abilities.end() && found->second.activeUntil > now;
}

float CCSOLEliteMonsterBehavior::WebSpeedDown() const
{ return Parameter(13, "SpeedDown"); }

bool CCSOLEliteMonsterBehavior::InstallWeb(CSOLMonsterSpawnCall& call, const EliteStatePtr& state,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, ConfigError& error)
{
    if (!runtime->players || !runtime->players->Active())
        return Fail(error, "elite web requires the active player registry");
    if (!runtime->eliteManager->SetWebActive(true)) return false;
    const auto access = runtime->session->Map().EntityAccess();
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, access));
        if (!Pin(call, state)) return false;
        if (!LivePlayer(player)) continue;
        const EHANDLE handle(player);
        const auto registered = runtime->players->Find(handle);
        if (!registered) return Fail(error, "elite web target has no authoritative player state");
        // The player service uses negative duration for an exact FLT_MAX
        // expiry; do not add the current time to an already maximal value.
        const auto label = runtime->players->InstallSkillLabel(handle, 3, 0, -1.0f);
        if (!Pin(call, state)) return false;
        if (!label) return Fail(error, "elite web label installation failed");
        state->webLabels.push_back({handle, registered->generation, registered->monsterLifeSerial, label});
        player = handle.Get<CBasePlayer>();
        if (!LivePlayer(player)) continue;
        player->ResetMaxSpeed();
        if (!Pin(call, state)) return false;
    }
    return true;
}

bool CCSOLEliteMonsterBehavior::ResetForSpawn(CSOLMonsterSpawnCall& call,
    CSOLMonsterSpawnState& spawn, ConfigError& error)
{
    if (!m_settings || !m_bindings.qualifiedPlayers || !m_bindings.replaceType ||
        !m_bindings.spawnFollower || !m_bindings.recoverHealth || !m_bindings.frostSlow ||
        !m_bindings.publishEntityEffect || !m_bindings.updateRecovery || !m_bindings.publishAreaEffect)
        return Fail(error, "elite behavior requires its settings and authoritative spawn bindings");
    if (!CCSOLOrdinaryMonsterBehavior::ResetForSpawn(call, spawn, error)) return false;
    auto state = m_elite ? std::make_shared<EliteState>(*m_elite) : std::make_shared<EliteState>();
    state->ordinary = SpawnState();
    m_elite = state;
    const auto runtime = Pin(call, state);
    if (!runtime || !gpGlobals || !std::isfinite(gpGlobals->time)) return false;
    state->lastDamage = state->targetChangedAt = gpGlobals->time;
    if (runtime->eliteManager->FollowerSpawn())
    {
        state->follower = true;
        const auto* primary = runtime->eliteManager->Primary();
        const auto* behavior = primary ? dynamic_cast<const CCSOLEliteMonsterBehavior*>(primary->Behavior()) : nullptr;
        if (!primary || primary->Type()->id != 32800 || !behavior || !behavior->m_elite ||
            behavior->m_elite->follower || !behavior->m_elite->abilities.contains(1)) return true;
        const auto snapshot = behavior->m_elite->initializedType;
        if (!snapshot) return Fail(error, "elite follower has no primary type snapshot");
        state->abilities.clear();
        for (const auto& [id, unused] : behavior->m_elite->abilities)
            state->abilities.emplace(id, csol::scenario::EliteAbilityClock{id});
        const bool replaced = m_bindings.replaceType(call, spawn, snapshot, error);
        if (!Pin(call, state)) return false;
        return replaced; // Original follower returns without reapplying attributes.
    }
    int qualified{};
    const bool counted = m_bindings.qualifiedPlayers(call, qualified, error);
    if (!Pin(call, state) || !counted) return false;
    struct RandomContext { CCSOLEliteMonsterBehavior* owner; CSOLMonsterSpawnCall* call; EliteStatePtr state; };
    RandomContext context{this, &call, state};
    const csol::scenario::EliteRandom random{&context, [](void* pointer, int low, int high, int& result)
    {
        auto& value = *static_cast<RandomContext*>(pointer);
        if (!value.owner->Pin(*value.call, value.state) || !g_engfuncs.pfnRandomLong) return false;
        result = RANDOM_LONG(low, high);
        return value.owner->Pin(*value.call, value.state) != nullptr;
    }};
    const int difficulty = runtime->session->Difficulty().SelectedRow();
    if (!csol::scenario::SelectEliteAbilities(difficulty, qualified, random, state->abilities, error) ||
        !Pin(call, state)) return false;
    std::shared_ptr<const csol::scenario::MonsterType> type;
    if (!csol::scenario::ApplyEliteInitialModifiers(*m_settings, difficulty, state->abilities,
            *call.Current()->Type(), type, error)) return false;
    const bool replaced = m_bindings.replaceType(call, spawn, type, error);
    if (!Pin(call, state) || !replaced) return false;
    if (state->abilities.contains(0))
    {
        spawn.attributes.attackInterval = type->attackInterval;
        spawn.attributes.speed = static_cast<float>(type->speed);
    }
    if (state->abilities.contains(2)) spawn.attributes.attackDamage = static_cast<float>(type->attackDamage);
    //109671F8 changes the type health only. It does not rewrite health/max_hp.
    if (state->abilities.contains(13) && !InstallWeb(call, state, runtime, error)) return false;
    if (state->abilities.contains(1))
    {
        const double count = std::trunc(static_cast<double>(Parameter(1, "Num")));
        if (!std::isfinite(count) || count < INT32_MIN || count > INT32_MAX)
            return Fail(error, "elite clone count exceeds int32");
        state->remainingClones = static_cast<int>(count);
        state->cloneAfter = gpGlobals->time + 1.0f;
    }
    state->initializedType = std::move(type);
    const auto* appearance = csol::scenario::FindMonsterAppearance(call.Current()->Type()->appearance);
    if (!appearance) return Fail(error, "elite appearance is unavailable");
    std::vector<int> selected;
    selected.reserve(state->abilities.size());
    for (const auto& [id, unused] : state->abilities) selected.push_back(id);
    const bool sent = csol::scenario::skills::PublishEliteAbilities(*runtime->session,
        appearance->index, selected, error);
    return Pin(call, state) && sent;
}

bool CCSOLEliteMonsterBehavior::BeforeDeath(CSOLMonsterDeathCall& call, ConfigError& error)
{
    const auto state = m_elite;
    const auto runtime = m_runtime.lock();
    const auto current = [&]
    {
        auto* monster = call.Current();
        return state && m_elite == state && runtime && !runtime->Closing() && runtime->session &&
            runtime->eliteManager && runtime->eliteManager->Current() &&
            call.BelongsTo(runtime->session->Map()) && monster && monster->Behavior() == this;
    };
    if (!current()) return false;
    if (!state->follower)
    {
        const bool hadWeb = runtime->eliteManager->WebActive();
        if (!runtime->eliteManager->SetWebActive(false)) return false;
        const EHANDLE victim(call.Current()), attacker(call.Attacker());
        const bool notified = runtime->eliteManager->PrimaryDied(victim, attacker, error);
        if (!current() || !notified) return false;
        state->cloneAfter = -1.0f;
        state->remainingClones = 0;
        if (hadWeb)
        {
            if (!runtime->players || !runtime->players->Active())
                return Fail(error, "elite death has no active web recipients");
            const auto access = runtime->session->Map().EntityAccess();
            for (int index = 1; index <= gpGlobals->maxClients; ++index)
            {
                auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(index, access));
                if (!current()) return false;
                if (!LivePlayer(player)) continue;
                const EHANDLE handle(player);
                const auto registered = runtime->players->Find(handle);
                if (registered)
                    for (const auto& label : state->webLabels)
                        if (label.player.Get<CBasePlayer>() == player && label.generation == registered->generation &&
                            label.life == registered->monsterLifeSerial)
                            if (!runtime->players->RemoveSkillLabel(handle, label.label)) return false;
                if (!current()) return false;
                player = handle.Get<CBasePlayer>();
                if (!LivePlayer(player)) continue;
                player->ResetMaxSpeed();
                if (!current()) return false;
            }
            state->webLabels.clear();
        }
    }
    return CCSOLOrdinaryMonsterBehavior::BeforeDeath(call, error);
}

bool CCSOLEliteMonsterBehavior::ResolvePreArmorDamage(CSOLMonsterDamageCall& call, ConfigError& error)
{
    const auto state = m_elite;
    const auto runtime = Pin(call.Spawn(), state);
    if (!runtime || !gpGlobals || !call.Current()) return false;
    if (call.damage > 0) state->lastDamage = gpGlobals->time;
    if (AbilityActive(8, gpGlobals->time)) { call.damage = 0; return true; }
    if (state->abilities.contains(11))
    {
        auto* player = call.Player();
        const bool directKnife = player && call.Inflictor() == player && player->m_pActiveItem &&
            !(player->m_pActiveItem->pev->flags & FL_KILLME) && player->m_pActiveItem->m_iId == WEAPON_KNIFE;
        float multiplier{};
        if (directKnife)
        {
            const double percent = static_cast<double>(Parameter(11,
                state->doubleSide ? "MeleeDmgDown" : "MeleeDmgUp")) / 100.0;
            multiplier = static_cast<float>(state->doubleSide ? 1.0 - percent : 1.0 + percent);
        }
        else
        {
            const float percent = Parameter(11, state->doubleSide ? "RangeDmgUp" : "RangeDmgDown") / 100.0f;
            multiplier = state->doubleSide ? percent + 1.0f : 1.0f - percent;
        }
        call.damage *= multiplier;
    }
    if (!std::isfinite(call.damage)) return Fail(error, "elite damage modifier is not finite");
    if (call.damage > 0 && call.Player())
    {
        if (!runtime->players) return Fail(error, "elite hit has no authoritative player registry");
        std::int32_t identity{};
        if (!runtime->players->ReadLocalIdentity(EHANDLE(call.Player()), identity, error) ||
            !runtime->eliteManager->RecordExecution(identity, error)) return false;
    }
    return Pin(call.Spawn(), state) && call.Current();
}

bool CCSOLEliteMonsterBehavior::UseSkill(CSOLMonsterSpawnCall& call, const StatePtr& ordinary,
    CCSOLOrdinaryMonsterServices& services)
{
    const auto state = m_elite;
    const auto runtime = Pin(call, state);
    if (!runtime || state->ordinary != ordinary || !gpGlobals) return false;
    for (auto& [ability, clock] : state->abilities)
    {
        //10968470 uses strict >, not >=. Each node owns independent clocks;
        // elite v1B0 always returns false to the original tree after dispatch.
        if (!(gpGlobals->time > clock.nextActivation)) continue;
        float cooldown{};
        if (!csol::scenario::StartEliteAbilityClock(*m_settings,
                runtime->session->Difficulty().SelectedRow(), state->abilities.contains(0),
                gpGlobals->time, clock, cooldown, ordinary->error)) return false;
        if (!ActivateAbility(call, state, runtime, ability, cooldown, services, ordinary->error) ||
            !Pin(call, state)) return false;
    }
    return true;
}

bool CCSOLEliteMonsterBehavior::ActivateAbility(CSOLMonsterSpawnCall& call,
    const EliteStatePtr& state, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    int ability, float cooldown, CCSOLOrdinaryMonsterServices& services, ConfigError& error)
{
    if (!Pin(call, state)) return false;
    switch (ability)
    {
    // Passive initialization/recovery/web effects have no10967E00 branch.
    case 0: case 1: case 2: case 9: case 10: case 13: return true;
    case 3: case 6: case 7:
    {
        float damage = Parameter(ability, "Damage");
        // Only the fireball captures109664C0's attack-up multiplier. The
        // firewall/thunder Think callbacks read their own raw damage later.
        if (ability == 3 && state->abilities.contains(2))
            damage *= 1.0f + Parameter(2, "Percent") / 100.0f;
        if (!std::isfinite(damage)) return Fail(error, "elite entity damage is invalid");
        return CreateAbilityEntity(call, state, runtime, ability, damage, error);
    }
    case 4: case 12: return AreaAbility(call, state, runtime, ability, services, error);
    case 5: return Teleport(call, state, error);
    case 8:
        return csol::scenario::skills::PublishEliteInvincible(*runtime->session,
            EHANDLE(call.Current()), Parameter(8, "Duration"), error) && Pin(call, state);
    case 11:
    {
        const bool oldSide = state->doubleSide;
        state->doubleSide = !oldSide;
        auto* monster = call.Current();
        const float height = monster->pev->absmax.z - monster->pev->origin.z;
        return csol::scenario::skills::PublishEliteSide(*runtime->session,
            EHANDLE(monster), oldSide, height, cooldown, error) && Pin(call, state);
    }
    default: return Fail(error, "unknown elite ability index");
    }
}

bool CCSOLEliteMonsterBehavior::CreateAbilityEntity(CSOLMonsterSpawnCall& call,
    const EliteStatePtr& state, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    int ability, float damage, ConfigError& error)
{
    if (!Pin(call, state) || !gpGlobals || !g_engfuncs.pfnRandomLong)
        return Fail(error, "elite auxiliary target random source is unavailable");
    if (!runtime->players || !runtime->players->Active())
        return Fail(error, "elite auxiliary targets require the player registry");
    Vector origin = call.Current()->pev->origin;
    Vector axis{};
    if (ability == 3)
    {
        const auto* appearance = csol::scenario::FindMonsterAppearance(call.Current()->Type()->appearance);
        if (!appearance) return Fail(error, "elite fireball launch appearance is unavailable");
        // Original v8C reads the appearance's eye-height field.
        origin.z = (origin.z + appearance->eyesZ) + 50.0f;
    }
    else if (ability == 6)
    {
        struct Candidate
        {
            EHANDLE player;
            std::optional<std::pair<std::uint64_t, std::uint64_t>> life;
        };
        std::vector<Candidate> candidates;
        if (!g_engfuncs.pfnFindEntityInSphere || !g_engfuncs.pfnEntOffsetOfPEntity)
            return Fail(error, "elite firewall requires engine sphere lookup");
        edict_t* cursor{};
        for (;;)
        {
            cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, 393.699981689453125f);
            if (!Pin(call, state)) return false;
            const bool exhausted = FNullEnt(cursor);
            if (!Pin(call, state)) return false;
            if (exhausted) break;
            if (cursor->free) continue;
            auto* player = dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(cursor));
            if (!LivePlayer(player) || player->pev->takedamage == DAMAGE_NO) continue;
            const EHANDLE handle(player);
            candidates.push_back({handle, runtime->players->MonsterSkillLife(handle)});
        }
        EHANDLE target;
        if (!candidates.empty())
        {
            const int selected = RANDOM_LONG(0, static_cast<int>(candidates.size()) - 1);
            if (!Pin(call, state)) return false;
            if (selected < 0 || static_cast<std::size_t>(selected) >= candidates.size())
                return Fail(error, "elite target random source is out of range");
            const auto& candidate = candidates[selected];
            if (runtime->players->MonsterSkillLife(candidate.player) == candidate.life) target = candidate.player;
        }
        auto* player = target.Get<CBasePlayer>();
        if (!LivePlayer(player)) return true;
        origin = player->pev->origin;
        const Vector delta = origin - call.Current()->pev->origin;
        const Vector perpendicular(-delta.y, delta.x, 0);
        axis = std::abs(perpendicular.x) > std::abs(perpendicular.y) ? Vector(1,0,0) : Vector(0,1,0);
    }
    else if (ability == 7)
    {
        const float range = static_cast<float>(static_cast<double>(Parameter(7, "Range")) *
            static_cast<double>(39.37f));
        const double maximum = std::trunc(static_cast<double>(range));
        if (!std::isfinite(maximum) || maximum < 0 || maximum > INT32_MAX)
            return Fail(error, "elite thunder random radius is invalid");
        const int bound = static_cast<int>(maximum);
        const int x = RANDOM_LONG(-bound, bound);
        if (!Pin(call, state)) return false;
        const int y = RANDOM_LONG(-bound, bound);
        if (!Pin(call, state)) return false;
        if (x < -bound || x > bound || y < -bound || y > bound)
            return Fail(error, "elite thunder random source is out of range");
        origin = call.Current()->pev->origin + Vector(static_cast<float>(x), static_cast<float>(y), 0);
    }
    const bool result = CCSOLEliteAbilityEntity::Create(call, runtime, m_settings,
        m_bindings.publishEntityEffect, ability, damage, origin, axis, error);
    return Pin(call, state) && result;
}

bool CCSOLEliteMonsterBehavior::AreaAbility(CSOLMonsterSpawnCall& call,
    const EliteStatePtr& state, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    int ability, CCSOLOrdinaryMonsterServices& services, ConfigError& error)
{
    if (!Pin(call, state) || !runtime->players || !runtime->players->Active() ||
        !g_engfuncs.pfnFindEntityInSphere || !g_engfuncs.pfnEntOffsetOfPEntity)
        return Fail(error, "elite area ability requires its entity and player services");
    if (!m_bindings.publishAreaEffect || (ability == 4 && !m_bindings.frostSlow))
        return Fail(error, "elite area ability effect or status53 consumer is unavailable");
    const auto readArea = [&]
    {
        return std::pair{static_cast<float>(static_cast<double>(Parameter(ability, "Range")) *
            static_cast<double>(39.37f)), Parameter(ability, "Damage")};
    };
    std::pair<float, float> area;
    if (ability == 12)
    {
        //10968817/10968869 initialize two local statics on the first pull
        // in the module lifetime. Later difficulty changes keep that range
        // and base damage. The two impulse statics initialize separately
        // below, only after the first actual recipient has taken damage.
        static const auto firstPull = readArea();
        area = firstPull;
    }
    else area = readArea();
    const auto [radius, baseDamage] = area;
    const float speedRatio = static_cast<float>(1.0 - static_cast<double>(Parameter(4, "SpeedPercent")) / 100.0);
    if (!std::isfinite(radius) || radius < 0 || !std::isfinite(baseDamage) || !std::isfinite(speedRatio))
        return Fail(error, "elite area parameters are invalid");
    edict_t* cursor{};
    for (;;)
    {
        auto* monster = call.Current();
        const auto origin = monster->pev->origin;
        cursor = FIND_ENTITY_IN_SPHERE(cursor, origin, radius);
        if (!Pin(call, state)) return false;
        const bool exhausted = FNullEnt(cursor);
        if (!Pin(call, state)) return false;
        if (exhausted) break;
        if (cursor->free) continue;
        CBaseEntity* next = CBaseEntity::Instance(cursor);
        auto* player = dynamic_cast<CBasePlayer*>(next);
        if (!LivePlayer(player) || player->pev->takedamage == DAMAGE_NO) continue;
        const EHANDLE target(player);
        const auto registered = runtime->players->Find(target);
        if (!registered) return Fail(error, "elite area target has no registered player state");
        const auto life = registered->monsterLifeSerial;
        float damage = baseDamage;
        if (state->abilities.contains(2)) damage *= 1.0f + Parameter(2, "Percent") / 100.0f;
        if (!std::isfinite(damage)) return Fail(error, "elite area damage is invalid");
        //11533370 uses the elite as both raw inflictor and attacker, bits0.
        // A normal TakeDamage rejection is not a failure of the area loop.
        player->TakeDamage(monster->pev, monster->pev, damage, 0);
        if (!Pin(call, state)) return false;
        player = target.Get<CBasePlayer>();
        if (!player || !player->pev || runtime->players->Find(target) != registered ||
            registered->monsterLifeSerial != life) continue;
        if (ability == 4)
        {
            const float speed = speedRatio * player->pev->maxspeed;
            const bool slowed = m_bindings.frostSlow(call, target,
                Parameter(4, "SpeedDownDuration"), speed, error);
            if (!Pin(call, state) || !slowed) return false;
        }
        else
        {
            //10968674 normalizes all three components before using XY.
            const Vector delta = call.Current()->pev->origin - player->pev->origin;
            const float squared = (delta.y * delta.y + delta.x * delta.x) + delta.z * delta.z;
            const float length = static_cast<float>(std::sqrt(static_cast<double>(squared)));
            const float inverse = length == 0 ? 0 : 1.0f / length;
            //109688B5/10968901 cache Z first, then horizontal power. An
            // empty first pulse does not initialize either of these values.
            static const auto firstImpulse = std::pair{Parameter(12, "PullPowerZ"),
                Parameter(12, "PullPower")};
            const float power = firstImpulse.second;
            const Vector velocity(delta.x * inverse * power, delta.y * inverse * power,
                firstImpulse.first);
            if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
                return Fail(error, "elite pull velocity is invalid");
            const bool pushed = services.ApplyContactImpulse(call, target, velocity, error);
            if (!Pin(call, state) || !pushed) return false;
        }
    }
    const bool sent = m_bindings.publishAreaEffect(call, ability, error);
    return Pin(call, state) && sent;
}

bool CCSOLEliteMonsterBehavior::Teleport(CSOLMonsterSpawnCall& call,
    const EliteStatePtr& state, ConfigError& error)
{
    if (!Pin(call, state) || !g_engfuncs.pfnTraceHull || !g_engfuncs.pfnSetOrigin)
        return Fail(error, "elite teleport requires actual engine hull traces");
    auto* player = state->ordinary->target.Get<CBasePlayer>();
    if (!LivePlayer(player)) return true;
    const Vector origin = player->pev->origin;
    auto* monster = call.Current();
    const Vector delta = monster->pev->origin - origin;
    const float squared = (delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z;
    if (!std::isfinite(squared)) return Fail(error, "elite teleport target is invalid");
    if (squared < 22500.0f) return true;
    //10969080 concatenates these three SIMD constants into four directions.
    // The candidate is intentionally carried across directions, not reset.
    const Vector directions[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}};
    Vector candidate = origin;
    bool found{};
    for (const auto& direction : directions)
    {
        if (found) break;
        for (int step = 0; step < 100; ++step)
        {
            TraceResult trace{};
            TRACE_HULL(candidate, candidate, dont_ignore_monsters, human_hull, monster->edict(), &trace);
            if (!Pin(call, state)) return false;
            if (trace.flFraction >= 1 && !trace.fStartSolid) { found = true; break; }
            candidate += direction * 20.0f;
            const Vector moved = candidate - origin;
            const float distance = (moved.y * moved.y + moved.x * moved.x) + moved.z * moved.z;
            if (!(distance <= 22500.0f)) break;
        }
    }
    if (found)
    {
        candidate.z += 1.0f;
        UTIL_SetOrigin(call.Current()->pev, candidate);
    }
    return Pin(call, state) != nullptr;
}

bool CCSOLEliteMonsterBehavior::UpdateBehavior(CSOLMonsterSpawnCall& call,
    const StatePtr& ordinary, CCSOLOrdinaryMonsterServices& services)
{
    const auto state = m_elite;
    const auto runtime = Pin(call, state);
    if (!runtime || state->ordinary != ordinary || !gpGlobals) return false;
    if (runtime->eliteManager->Disabled()) return true;
    if (!CCSOLOrdinaryMonsterBehavior::UpdateBehavior(call, ordinary, services) || !Pin(call, state)) return false;
    auto& error = ordinary->error;
    if (state->abilities.contains(10) && gpGlobals->time > state->lastRecovery + 1.0f)
    {
        if (!m_bindings.recoverHealth)
            return Fail(error, "elite regeneration requires the real monster recovery consumer");
        state->lastRecovery = gpGlobals->time;
        float health = Parameter(10, "HP");
        if (gpGlobals->time - state->lastDamage > Parameter(10, "BonusTime"))
            health *= Parameter(10, "Bonus");
        CSOLMonsterDeathContext context;
        //10967B10=0x01010000 and B1E=0x100:18/19/21 are1;
        // the zero byte at20 remains distinct from player skill damage.
        const bool recovered = m_bindings.recoverHealth(call, health, context, error);
        if (!Pin(call, state) || !recovered) return false;
    }
    if (state->abilities.contains(1) && state->remainingClones > 0 && gpGlobals->time > state->cloneAfter)
    {
        if (!m_bindings.spawnFollower)
            return Fail(error, "elite clone requires its scenario spawn-group dispatcher");
        state->cloneAfter = gpGlobals->time + 2.0f;
        --state->remainingClones;
        const bool spawned = m_bindings.spawnFollower(call, error);
        if (!Pin(call, state) || !spawned) return false;
    }
    if (!state->follower)
    {
        const auto* monster = call.Current();
        const float ratio = monster->pev->health / monster->pev->max_health;
        if (!std::isfinite(ratio)) return Fail(error, "elite health ratio is invalid");
        if (ratio != state->healthRatio)
        {
            state->healthRatio = ratio;
            const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
            if (!appearance) return Fail(error, "elite health appearance is unavailable");
            const bool sent = csol::scenario::skills::PublishEliteHealth(*runtime->session,
                EHANDLE(call.Current()), appearance->index, ratio, error);
            if (!Pin(call, state) || !sent) return false;
        }
    }
    if (!m_bindings.updateRecovery)
        return Fail(error, "elite navigation recovery requires its original scenario target gates");
    bool relocate{};
    const bool updated = m_bindings.updateRecovery(call, *ordinary,
        state->targetChangedAt, state->lastTarget, relocate, error);
    if (!Pin(call, state) || !updated) return false;
    return !relocate || Teleport(call, state, error);
}
