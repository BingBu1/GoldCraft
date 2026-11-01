// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_skills.h"
#include "scenario_monster_factory.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_player_runtime.h"
#include "game_rules_gate.h"
#include "characters.h"

#include <cmath>
#include <cstdlib>

#pragma clang fp contract(off)

class CCSOLMonsterSkillEntity;
enum class MonsterSkillEntityKind { Trap, Disarm };

struct CSOLMonsterSkillEntityState
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    EHANDLE entity, creator, target;
    const CCSOLMonsterSkillEntity* identity{};
    std::uint64_t round{};
    float deadline{};
    MonsterSkillEntityKind kind{};
    bool spawned{}, ready{}, busy{}, retired{}, holding{};

    CCSOLMonsterSkillEntity* Attached() const;
    CCSOLMonsterSkillEntity* Current() const;
    void Remove();
};

class CCSOLMonsterSkillEntity final : public CBaseAnimating
{
public:
    void Spawn() override;
    void OnDestroy() override;
    void FlightExpired();
    void DisarmTouch(CBaseEntity* other);
    void TrapThink();
    void TrapTouch(CBaseEntity* other);
    void TrapKill();
    static bool Create(MonsterSkillEntityKind kind, CSOLMonsterSpawnCall& call,
        const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
        const Vector& direction, bool& created, csol::scenario::ConfigError& error);
private:
    friend struct CSOLMonsterSkillEntityState;
    std::shared_ptr<CSOLMonsterSkillEntityState> m_state;
};

LINK_ENTITY_TO_CLASS(zombitrap, CCSOLMonsterSkillEntity, CCSEntity)
LINK_ENTITY_TO_CLASS(deimostail, CCSOLMonsterSkillEntity, CCSEntity)

CCSOLMonsterSkillEntity* CSOLMonsterSkillEntityState::Attached() const
{
    if (retired || !access || !access->Allowed()) return nullptr;
    auto* current = dynamic_cast<CCSOLMonsterSkillEntity*>(entity.Get<CBaseEntity>());
    return current && current == identity && current->m_state.get() == this &&
        !(current->pev->flags & FL_KILLME) ? current : nullptr;
}
CCSOLMonsterSkillEntity* CSOLMonsterSkillEntityState::Current() const
{
    const auto owner = runtime.lock();
    if (!owner || owner->Closing() || owner->session != session || !session ||
        session->Map().EntityAccess() != access || session->Map().RoundGeneration() != round)
        return nullptr;
    return Attached();
}
void CSOLMonsterSkillEntityState::Remove()
{
    if (auto* current = Attached())
    {
        retired = true;
        current->SetThink(nullptr);
        current->SetTouch(nullptr);
        current->pev->nextthink = 0;
        UTIL_Remove(current);
    }
}

namespace
{
struct SkillEntityScope
{
    std::shared_ptr<CSOLMonsterSkillEntityState> state;
    explicit SkillEntityScope(std::shared_ptr<CSOLMonsterSkillEntityState> value) : state(std::move(value))
    { state->busy = true; }
    ~SkillEntityScope() { state->busy = false; }
};
float Square(const Vector& value)
{ return (value.y * value.y + value.x * value.x) + value.z * value.z; }
bool LivingPlayer(const CBasePlayer* player)
{
    return player && !player->has_disconnected && player->pev->deadflag == DEAD_NO &&
        player->pev->health > 0 && !(player->pev->flags & (FL_KILLME | FL_DORMANT));
}
}

bool CCSOLMonsterSkillEntity::Create(MonsterSkillEntityKind kind, CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
    const Vector& direction, bool& created, csol::scenario::ConfigError& error)
{
    created = false;
    const auto session = runtime ? runtime->session : nullptr;
    if (!runtime || runtime->Closing() || !session || !call.BelongsTo(session->Map()) || !call.Current())
        return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    const auto access = session->Map().EntityAccess();
    const auto valid = [&]
    { return sdk.Current() && !runtime->Closing() && runtime->session == session && call.Current(); };
    if (!valid() || !gpGlobals || !g_engfuncs.pfnCreateNamedEntity || !g_engfuncs.pfnAllocString)
        return false;
    const Vector angles = kind == MonsterSkillEntityKind::Trap ? g_vecZero : call.Current()->pev->angles;
    const EHANDLE creator(call.Current());
    const int team = call.Current()->pev->team;
    const auto name = ALLOC_STRING(kind == MonsterSkillEntityKind::Trap ? "zombitrap" : "deimostail");
    if (!valid()) return false;
    auto* edict = CREATE_NAMED_ENTITY(name);
    if (!access->Allowed()) return false;
    const EHANDLE allocated(edict);
    const auto discard = [&]
    {
        if (!access->Allowed()) return;
        auto* entity = dynamic_cast<CCSOLMonsterSkillEntity*>(allocated.Get<CBaseEntity>());
        if (entity && !entity->m_state && !(entity->pev->flags & FL_KILLME)) UTIL_Remove(entity);
    };
    if (!valid()) { discard(); return false; }
    const bool absent = FNullEnt(edict);
    if (!valid()) { discard(); return false; }
    if (absent) return true;
    auto* entity = dynamic_cast<CCSOLMonsterSkillEntity*>(allocated.Get<CBaseEntity>());
    if (!entity || entity->m_state)
    { error.message = "scenario monster skill entity allocation has invalid private data"; return false; }
    const auto state = std::make_shared<CSOLMonsterSkillEntityState>();
    state->runtime = runtime;
    state->session = session;
    state->access = access;
    state->round = session->Map().RoundGeneration();
    state->entity = entity;
    state->creator = creator;
    state->identity = entity;
    state->kind = kind;
    entity->m_state = state;
    entity->pev->owner = creator.Get();
    entity->pev->angles = angles;
    entity->pev->team = team;
    UTIL_SetOrigin(entity->pev, origin);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    const int dispatched = DispatchSpawn(edict);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    if (dispatched < 0 || !state->spawned)
    { state->Remove(); error.message = "scenario monster skill entity Spawn failed"; return false; }
    entity = state->Current();
    if (kind == MonsterSkillEntityKind::Disarm)
    {
        entity->pev->velocity = direction * 1200.0f;
        entity->pev->movetype = MOVETYPE_FLY;
        entity->pev->solid = SOLID_BBOX;
        entity->pev->effects &= ~EF_NODRAW;
        UTIL_SetSize(entity->pev, g_vecZero, g_vecZero);
        if (!valid() || !state->Current()) { state->Remove(); return false; }
        state->deadline = gpGlobals->time + 0.5f;
        state->Current()->SetThink(&CCSOLMonsterSkillEntity::FlightExpired);
        state->Current()->pev->nextthink = state->deadline;
        const int sprite = MODEL_INDEX(CSOLMonsterDisarmTrail);
        if (!valid() || !state->Current()) { state->Remove(); return false; }
        const int index = state->Current()->entindex();
        if (!valid() || !state->Current()) { state->Remove(); return false; }
        MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
        WRITE_BYTE(TE_BEAMFOLLOW);
        WRITE_SHORT(index);
        WRITE_SHORT(sprite);
        WRITE_BYTE(20);
        WRITE_BYTE(1);
        WRITE_BYTE(250);
        WRITE_BYTE(200);
        WRITE_BYTE(0);
        WRITE_BYTE(255);
        MESSAGE_END();
    }
    else
    {
        // 11541E20 only supplies initial throw velocity for a player owner.
        // This producer is an ordinary monster, so its native early return
        // leaves the freshly spawned zero velocity untouched.
        state->deadline = gpGlobals->time + 30.0f;
    }
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    state->ready = true;
    created = true;
    return true;
}

void CCSOLMonsterSkillEntity::Spawn()
{
    const auto state = m_state;
    if (!state || !state->Current() || !gpGlobals) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current()) return;
    const bool trap = state->kind == MonsterSkillEntityKind::Trap;
    pev->movetype = trap ? MOVETYPE_PUSHSTEP : MOVETYPE_NONE;
    pev->solid = trap ? SOLID_TRIGGER : SOLID_NOT;
    pev->takedamage = DAMAGE_NO;
    pev->sequence = 0;
    pev->frame = 0;
    pev->framerate = 1;
    if (!trap) pev->effects |= EF_NODRAW;
    SET_MODEL(edict(), trap ? CSOLMonsterTrapModel : CSOLMonsterDisarmModel);
    auto* current = sdk.Current() ? state->Current() : nullptr;
    if (!current) return;
    const Vector extent = trap ? Vector(1, 1, 1) : g_vecZero;
    UTIL_SetSize(current->pev, -extent, extent);
    current = sdk.Current() ? state->Current() : nullptr;
    if (!current) return;
    if (trap)
    {
        current->pev->rendermode = kRenderTransAlpha;
        current->pev->renderamt = 120;
        current->pev->rendercolor = g_vecZero;
        current->SetTouch(&CCSOLMonsterSkillEntity::TrapTouch);
        current->SetThink(&CCSOLMonsterSkillEntity::TrapThink);
        current->pev->nextthink = gpGlobals->time + 0.1f;
        const bool reset = CSOL_ResetScenarioSequenceInfo(*current, [](const void* value) -> CBaseAnimating*
        { return static_cast<const CSOLMonsterSkillEntityState*>(value)->Current(); }, state.get());
        if (!reset || !sdk.Current() || !state->Current()) return;
    }
    else current->SetTouch(&CCSOLMonsterSkillEntity::DisarmTouch);
    state->spawned = true;
}

void CCSOLMonsterSkillEntity::TrapThink()
{
    const auto state = m_state;
    if (!state || state->busy || state->retired) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!state->ready || !gpGlobals) return;
    SkillEntityScope busy(state);
    pev->nextthink = gpGlobals->time + 0.1f;
    if (gpGlobals->time > state->deadline || (state->holding && !state->creator.Get<CBaseEntity>()))
    { SetThink(&CCSOLMonsterSkillEntity::TrapKill); return; }
    // HoldThink's damage branches require a player owner. A scenario monster
    // owner does not enter them; the effect17 timer owns the five-second bind.
    if (state->holding || !(pev->flags & FL_ONGROUND)) return;
    const auto runtime = state->runtime.lock();
    const auto players = runtime ? runtime->players : nullptr;
    if (!players) { state->Remove(); return; }
    EHANDLE nearest;
    float closest = 500.0f * 500.0f;
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* player = UTIL_PlayerByIndex(index);
        if (!sdk.Current() || !state->Current()) return;
        if (!LivingPlayer(player) || static_cast<int>(player->m_iTeam) == pev->team ||
            players->MonsterTrapActive(EHANDLE(player), gpGlobals->time)) continue;
        const float distance = Square(player->pev->origin - pev->origin);
        if (distance < closest) { closest = distance; nearest = player; }
    }
    const auto* player = nearest.Get<CBasePlayer>();
    if (!player) return; // Native path preserves the previous planar velocity.
    const Vector delta = player->pev->origin - pev->origin;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(Square(delta))));
    const float inverse = length == 0 ? 0.0f : 1.0f / length;
    pev->velocity.x = delta.x * inverse * 50.0f;
    pev->velocity.y = delta.y * inverse * 50.0f;
}

void CCSOLMonsterSkillEntity::TrapTouch(CBaseEntity* other)
{
    const auto state = m_state;
    if (!state || state->busy || state->retired || !other) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!state->ready || state->holding || !gpGlobals || !(pev->flags & FL_ONGROUND)) return;
    auto* player = dynamic_cast<CBasePlayer*>(other);
    const auto runtime = state->runtime.lock();
    const auto players = runtime ? runtime->players : nullptr;
    if (!players || !LivingPlayer(player) || static_cast<int>(player->m_iTeam) == pev->team) return;
    const EHANDLE target(player);
    if (players->MonsterTrapActive(target, gpGlobals->time)) return;
    SkillEntityScope busy(state);
    bool caught{};
    csol::scenario::ConfigError error;
    struct Capture
    {
        const CSOLScenarioSdkScope& sdk;
        const std::shared_ptr<CSOLMonsterSkillEntityState>& state;
        const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players;
        EHANDLE target;
        std::optional<std::pair<std::uint64_t, std::uint64_t>> life;
    } capture{sdk, state, players, target, players->MonsterSkillLife(target)};
    const CSOLMonsterSkillGuard guard{[](const void* value)
    {
        const auto& capture = *static_cast<const Capture*>(value);
        const auto runtime = capture.state->runtime.lock();
        return capture.sdk.Current() && capture.state->Current() && runtime && runtime->players == capture.players &&
            capture.life && capture.players->MonsterSkillLife(capture.target) == capture.life &&
            LivingPlayer(dynamic_cast<CBasePlayer*>(capture.target.Get<CBaseEntity>()));
    }, &capture};
    const auto prepare = [&]
    {
        auto* current = state->Current();
        if (!guard.Current() || !current) return false;
        current->SetTouch(nullptr);
        current->pev->solid = SOLID_NOT;
        current->pev->sequence = 1;
        current->pev->frame = 0;
        const bool reset = CSOL_ResetScenarioSequenceInfo(*current, [](const void* value) -> CBaseAnimating*
        { return static_cast<const CSOLMonsterSkillEntityState*>(value)->Current(); }, state.get());
        if (!reset || !guard.Current()) return false;
        current = state->Current();
        current->pev->rendermode = kRenderNormal;
        current->pev->renderfx = kRenderFxNone;
        current->pev->rendercolor = g_vecZero;
        bool male{};
        auto* victim = target.Get<CBasePlayer>();
        const bool known = CSOL_CharacterMale(victim, male, state->access);
        if (!guard.Current() || target.Get<CBasePlayer>() != victim) return false;
        if (!known)
        { error.message = "scenario trapped player has no character gender"; return false; }
        EMIT_SOUND_DYN(victim->edict(), CHAN_BODY,
            male ? "zombi/Zombi_Trapped.wav" : "zombi/Zombi_Trapped_Female.wav", 1.0f, 0.8f, 0, 100);
        if (!guard.Current()) return false;
        return true;
    };
    const bool applied = players->TryMonsterTrap(target, 5.0f, guard, prepare, caught, error);
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    if (!applied)
    {
        if (!error.message.empty()) CSOL_RejectGameRules(error.message.c_str());
        state->Remove();
        return;
    }
    if (!caught) return;
    state->session->Messages().SendMonsterSkillShake(target.Get());
    if (!guard.Current()) { state->Remove(); return; }
    state->session->Messages().SendMonsterTrapDirection(target.Get(), target.Get<CBasePlayer>()->pev->origin);
    if (!guard.Current()) { state->Remove(); return; }
    state->target = target;
    state->holding = true;
    state->deadline = gpGlobals->time + 5.0f;
    pev->nextthink = gpGlobals->time + 0.1f;
}

void CCSOLMonsterSkillEntity::TrapKill()
{
    const auto state = m_state;
    if (!state) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (sdk.Current() && state->Current())
        EMIT_SOUND_DYN(edict(), CHAN_BODY, "zombi/Heavy_newTrap.wav", 0, 0, SND_STOP, 100);
    state->Remove();
}

void CCSOLMonsterSkillEntity::FlightExpired()
{
    const auto state = m_state;
    if (!state || state->retired) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (sdk.Current() && state->Current())
    {
        const Vector origin = pev->origin;
        SetThink(nullptr);
        SetTouch(nullptr);
        state->session->Messages().SendMonsterDisarmImpact(origin);
    }
    state->Remove();
}

void CCSOLMonsterSkillEntity::DisarmTouch(CBaseEntity* other)
{
    const auto state = m_state;
    if (!state || state->busy || state->retired || !state->ready) return;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(state->session.get()));
    if (!sdk.Current() || !state->Current()) { state->Remove(); return; }
    SkillEntityScope busy(state);
    auto* player = dynamic_cast<CBasePlayer*>(other);
    if (!LivingPlayer(player)) { FlightExpired(); return; }
    const EHANDLE target(player);
    const auto runtime = state->runtime.lock();
    const auto players = runtime ? runtime->players : nullptr;
    const auto life = players ? players->MonsterSkillLife(target) : std::nullopt;
    const auto current = [&]
    {
        return sdk.Current() && state->Current() && runtime && runtime->players == players &&
            life && players->MonsterSkillLife(target) == life &&
            target.Get<CBaseEntity>() == player && LivingPlayer(player);
    };
    if (!current()) { FlightExpired(); return; }
    SetTouch(nullptr);
    pev->solid = SOLID_NOT;
    EMIT_SOUND_DYN(player->edict(), CHAN_BODY, "zombi/deimos_skill_hit.wav", 1.0f, 0.8f, 0, 100);
    if (!current()) { state->Remove(); return; }
    // CSO passes the struck human as both participants, including zero damage.
    player->TakeDamage(player->pev, player->pev, 0.0f, DMG_BULLET);
    if (!current()) { state->Remove(); return; }
    state->session->Messages().SendMonsterSkillShake(player->edict());
    if (!current()) { state->Remove(); return; }
    if (!g_engfuncs.pfnRandomFloat) { state->Remove(); return; }
    const float choice = RANDOM_FLOAT(0.0f, 100.0f);
    if (!current()) { state->Remove(); return; }
    if (choice <= 50.0f)
    {
        const auto tables = state->session->Equipment();
        const auto* map = state->session->Map().Tables();
        const auto equipment = state->session->EquipmentState();
        if (!tables || !tables->weaponDropDifficulty || !map || !map->mapId || !equipment)
        {
            CSOL_RejectGameRules("scenario disarm requires loaded weapon-drop rules and map identity");
            state->Remove();
            return;
        }
        if (!tables->WeaponDropRestricted(*map->mapId, state->session->Difficulty().SelectedRow()))
        {
            float resistance = 0;
            equipment->WeaponDropResistance(target, resistance);
            if (!current() || state->session->EquipmentState() != equipment) { state->Remove(); return; }
            // This second lottery is the original CRT source, independent of
            // the engine's 50% gate. Keep float multiplication and strict >.
            static_assert(RAND_MAX == 32767);
            const float roll = static_cast<float>(std::rand()) * (1.0f / 32767.0f);
            if (roll > resistance && !players->MonsterWeaponDropBlocked(target, gpGlobals->time))
            {
                auto* weapon = player->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT];
                if (!weapon) weapon = player->m_rgpPlayerItems[PISTOL_SLOT];
                if (weapon)
                {
                    const std::string name = STRING(weapon->pev->classname);
                    if (!current()) { state->Remove(); return; }
                    // Preserve native CanDrop/VIP/ammunition/weaponbox hooks;
                    // a refused primary does not fall through to the pistol.
                    auto* dropped = player->DropPlayerItem(name.c_str());
                    if (!current()) { state->Remove(); return; }
                    CWeaponBox* box{};
                    // A Drop hook can return a pointer it has already freed.
                    // Locate live private data before RTTI or dereferencing it.
                    for (int i = gpGlobals->maxClients + 1; dropped && i < gpGlobals->maxEntities; ++i)
                    {
                        auto* entry = INDEXENT(i);
                        if (!current()) { state->Remove(); return; }
                        if (entry && !entry->free && entry->pvPrivateData == dropped)
                        { box = dynamic_cast<CWeaponBox*>(dropped); break; }
                    }
                    if (box && !(box->pev->flags & FL_KILLME))
                    {
                        const EHANDLE boxHandle(box);
                        UTIL_MakeVectors(player->pev->angles);
                        if (!current() || boxHandle.Get<CBaseEntity>() != box || (box->pev->flags & FL_KILLME))
                        { state->Remove(); return; }
                        box->pev->velocity = gpGlobals->v_forward * 600.0f;
                    }
                }
            }
            else if (!(roll > resistance))
            {
                ClientPrint(player->pev, HUD_PRINTCENTER, "#CSO_ZBSVER2_YouResistedWeaponDrop");
                if (!current()) { state->Remove(); return; }
            }
        }
    }
    FlightExpired();
}

void CCSOLMonsterSkillEntity::OnDestroy()
{
    // GoldSrc frees private data without invoking this C++ destructor.
    const auto state = std::move(m_state);
    if (state) state->retired = true;
    CBaseAnimating::OnDestroy();
}

bool CSOL_CreateMonsterTrap(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, csol::scenario::ConfigError& error)
{
    const auto* monster = call.Current();
    if (!monster) return false;
    bool created{};
    return CCSOLMonsterSkillEntity::Create(MonsterSkillEntityKind::Trap, call, runtime,
        monster->pev->origin, g_vecZero, created, error);
}

bool CSOL_CreateMonsterDisarm(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
    const Vector& direction, bool& created, csol::scenario::ConfigError& error)
{
    return CCSOLMonsterSkillEntity::Create(MonsterSkillEntityKind::Disarm, call, runtime,
        origin, direction, created, error);
}
