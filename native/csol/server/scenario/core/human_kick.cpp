// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "human_kick.h"
#include "human_kick_damage.h"
#include "game_rules_gate.h"
#include "scenario_runtime.h"
#include "scenario_session.h"
#include "scenario_player_runtime.h"
#include "../../scenario_character_skills/host.hpp"
#include "../../../scenario/core/human_kick.hpp"
#include "../monsters/scenario_monster_factory.h"
#include "../../custom/action_stamina.h"
#include "../../../scenario/custom/action_resume.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstring>
#include <string>
#include "sound.h"

#pragma clang fp contract(off)

namespace
{
namespace kick = csol::scenario::human_kick;
using csol::scenario::ConfigError;
struct ColdResources
{
    std::weak_ptr<const CSOLScenarioEntityAccess> access;
    int message{}, model{};
    string_t viewmodel{};
    std::uint64_t generation{};
};
ColdResources resources;
std::uint64_t resourceGeneration{};
std::uint32_t nextLife{};
std::uint64_t nextAction{};
bool preparing{}, sending{};
bool policyEnabled{true};

struct State;
std::array<std::shared_ptr<State>, 33> states;
std::array<bool, 33> operating{};
struct State
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::weak_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> life;
    EHANDLE player, weapon;
    std::uint64_t generation{}, round{}, spawn{}, actorGeneration{}, nativeSpawn{};
    std::uint32_t revision{};
    csol::scenario::custom::ActionResumeToken action;
    string_t savedViewmodel{}, savedWeaponmodel{};
    float startedAt{}, savedOwnerAttack{}, savedFov{};
    int savedAnimation{}, savedIntFov{};
    int nativeSpawns{}, modelIndex{}, activeModel{}, sequence{-1}, slot{};
    std::uint8_t sentFlags{255};
    kick::Clock clock;
    bool busy{}, retired{}, locked{}, resumable{}, restoring{};

    CBasePlayer* Current() const
    {
        if (retired || slot < 1 || slot > 32 || states[slot].get() != this ||
            !access || !access->Allowed() || preparing || resources.generation != generation ||
            resources.access.lock() != access) return nullptr;
        const auto owner = runtime.lock();
        const auto profiles = players.lock();
        if (!owner || owner->Closing() || !owner->session || owner->players != profiles ||
            !profiles || !profiles->Active() || owner->session->Map().EntityAccess() != access ||
            owner->session->Map().RoundGeneration() != round || profiles->Find(player) != life ||
            !life || life->generation != actorGeneration || life->skillLifeSerial != spawn ||
            life->nativeSpawnSerial != nativeSpawn) return nullptr;
        auto* value = player.Get<CBasePlayer>();
        return value && value->pev && !value->has_disconnected && value->m_iNumSpawns == nativeSpawns &&
            !(value->pev->flags & (FL_KILLME | FL_DORMANT)) ? value : nullptr;
    }
};
struct Busy
{
    bool& value;
    explicit Busy(bool& flag) : value(flag) { value = true; }
    ~Busy() { value = false; }
};
bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

std::shared_ptr<State> Find(CBasePlayer& player)
{
    const auto access = resources.access.lock();
    if (!access || !access->Allowed()) return {};
    const EHANDLE handle(&player);
    const int index = player.entindex();
    if (!access->Allowed() || handle.Get<CBaseEntity>() != &player || index < 1 || index > 32) return {};
    const auto state = states[index];
    return state && !state->restoring && state->Current() == &player ? state : nullptr;
}
std::shared_ptr<State> Prepare(const std::shared_ptr<CCSOLScenarioRuntime>& runtime, CBasePlayer& player)
{
    if (!runtime || runtime->Closing() || !runtime->session || !runtime->players ||
        !runtime->players->Active()) return {};
    const auto session = runtime->session;
    const auto access = session->Map().EntityAccess();
    if (!access || !access->Allowed() || preparing || resources.access.lock() != access ||
        !resources.message || !resources.model) return {};
    const EHANDLE handle(&player);
    const auto profile = runtime->players->Find(handle);
    if (!profile) return {};
    const int index = player.entindex();
    if (!access->Allowed() || runtime->Closing() || runtime->session != session ||
        runtime->players->Find(handle) != profile || handle.Get<CBaseEntity>() != &player ||
        index < 1 || index > 32) return {};
    auto state = states[index];
    if (state && state->restoring) return {};
    if (!state || state->Current() != &player)
    {
        if (nextLife == UINT32_MAX) return {};
        const auto previous = state;
        state = std::make_shared<State>();
        state->runtime = runtime; state->players = runtime->players;
        state->access = access; state->life = profile; state->player = handle;
        state->generation = resources.generation;
        state->round = session->Map().RoundGeneration();
        state->spawn = profile->skillLifeSerial; state->actorGeneration = profile->generation;
        state->nativeSpawn = profile->nativeSpawnSerial; state->nativeSpawns = player.m_iNumSpawns;
        state->revision = ++nextLife;
        state->slot = index;
        // Cancelling the action in the same life does not grant another kick.
        if (previous && previous->access == access && previous->life == profile &&
            previous->player.Get<CBaseEntity>() == &player && previous->spawn == profile->skillLifeSerial &&
            previous->nativeSpawn == profile->nativeSpawnSerial && previous->nativeSpawns == player.m_iNumSpawns)
        {
            state->clock = previous->clock;
            state->clock.Finish();
        }
        states[index] = state;
    }
    if (state->modelIndex != player.pev->modelindex)
    {
        const int model = player.pev->modelindex;
        const int sequence = player.LookupSequence("zombiekick");
        if (state->Current() != &player || player.pev->modelindex != model) return {};
        state->modelIndex = model;
        state->sequence = sequence;
    }
    return state;
}
bool Enabled(const State& state, const CBasePlayer& player)
{
    const auto runtime = state.runtime.lock();
    return policyEnabled && !state.restoring && runtime && runtime->session->State() == CSOLScenarioSessionState::Active &&
        state.sequence >= 0 && player.pev->deadflag == DEAD_NO && player.pev->health > 0 &&
        !player.pev->iuser1 && !(player.pev->flags & FL_FROZEN);
}
bool Ready(const State& state, CBasePlayer& player)
{
    if (!Enabled(state, player) || !gpGlobals || !std::isfinite(gpGlobals->time) ||
        state.clock.ActiveNow() || !nextLife || nextAction == UINT64_MAX) return false;
    auto* item = dynamic_cast<CBasePlayerWeapon*>(player.m_pActiveItem);
    const auto runtime = state.runtime.lock();
    // The user explicitly permits emergency kicks during attacks, reloads and
    // aiming. Weapon timers continue to belong to the interrupted weapon.
    return item && !(item->pev->flags & FL_KILLME) && runtime &&
        CSOL_ActionStaminaCanKick(runtime, player);
}
bool Publish(const std::shared_ptr<State>& state, bool force = false)
{
    auto* player = state->Current();
    if (!player || !gpGlobals) return false;
    std::uint8_t flags{};
    if (Enabled(*state, *player))
    {
        flags = kick::Enabled;
        if (state->clock.ActiveNow()) flags |= kick::Active;
        else if (Ready(*state, *player)) flags |= kick::Ready;
    }
    if (!force && state->sentFlags == flags) return true;
    const auto bytes = kick::Encode({state->revision, flags, state->clock.Remaining(gpGlobals->time)});
    if (!bytes || sending || resources.message < 64 || resources.message > 255) return false;
    Busy guard(sending);
    MESSAGE_BEGIN(MSG_ONE, resources.message, nullptr, player->pev);
    for (const auto byte : *bytes) WRITE_BYTE(std::to_integer<unsigned>(byte));
    MESSAGE_END();
    if (!state->Current()) return false;
    state->sentFlags = flags;
    return true;
}
bool Animation(const std::shared_ptr<State>& state, int sequence)
{
    auto* player = state->Current();
    if (!player || sending || !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnMessageEnd ||
        !g_engfuncs.pfnWriteByte) return false;
    const int body = player->pev->body;
    Busy guard(sending);
    MESSAGE_BEGIN(MSG_ONE, SVC_WEAPONANIM, nullptr, player->pev);
    WRITE_BYTE(sequence); WRITE_BYTE(body);
    MESSAGE_END();
    return state->Current() != nullptr;
}
bool Finish(const std::shared_ptr<State>& state, bool restore)
{
    if (state->restoring) return false;
    Busy restoration(state->restoring);
    const bool wasActive = state->clock.ActiveNow();
    state->clock.Finish();
    auto* player = state->Current();
    if (!player) return false;
    auto* item = state->weapon.Get<CBasePlayerItem>();
    const bool ownsLock = state->locked && player->pev->viewmodel == resources.viewmodel &&
        player->m_flNextAttack > 100000.0f;
    state->locked = false;
    const bool resume = wasActive && restore && item && player->m_pActiveItem == item &&
        player->pev->deadflag == DEAD_NO && player->pev->health > 0;
    bool applied = true;
    if (auto* action = state->resumable ?
        dynamic_cast<csol::scenario::custom::ResumableWeaponAction*>(item) : nullptr)
    {
        if (resume)
        {
            const auto result = action->ResumeWeaponAction(state->action);
            applied = result == csol::scenario::custom::ActionResumeResult::Applied;
            if (result == csol::scenario::custom::ActionResumeResult::Rejected && ownsLock &&
                state->Current() == player && player->m_pActiveItem == item &&
                player->pev->viewmodel == resources.viewmodel && player->m_flNextAttack > 100000.0f)
            {
                // A rejected observer may have already committed the real
                // reload. Retain this overlay for a presentation-only retry;
                // never lose the token or unlock a still-suspended guest gun.
                state->locked = true;
                state->clock.BeginEmergency(gpGlobals->time);
                return true;
            }
        }
        else action->CancelWeaponAction(state->action);
        player = state->Current();
        if (!player || state->weapon.Get<CBasePlayerItem>() != item) return false;
    }
    if (resume && (!state->resumable || !applied))
    {
        // Resume native presentation only. Deploy would restart its cooldown
        // and may discard/repeat the in-flight reload or attack.
        player->pev->viewmodel = state->savedViewmodel;
        player->pev->weaponmodel = state->savedWeaponmodel;
        player->pev->weaponanim = state->savedAnimation;
        if (player->pev->fov == 90) player->pev->fov = state->savedFov;
        if (player->m_iFOV == 90) player->m_iFOV = state->savedIntFov;
        const bool restored = Animation(state, state->savedAnimation);
        applied = applied && restored;
        player = state->Current();
        if (!player || state->weapon.Get<CBasePlayerItem>() != item) return false;
    }
    state->resumable = false; state->action = {};
    // A callback may have selected another weapon or installed another timer.
    // Preserve it. Only the still-owned large action lock is released here.
    if (ownsLock && player->m_pActiveItem == item && player->m_flNextAttack > 100000.0f)
        player->m_flNextAttack = std::max(-1.0f,
            state->savedOwnerAttack - (gpGlobals->time - state->startedAt));
    return applied;
}

bool ImpactSound(const std::shared_ptr<State>& state, const Vector& start,
    const Vector& end, const TraceResult& trace, bool monsterHit)
{
    auto* player = state->Current();
    if (!player) return false;
    const char* sound = kick::SwingSound;
    if (monsterHit) sound = kick::HitSounds[4]; // User adaptation of the original player-hit stone1.
    else if (trace.flFraction < 1.0f)
    {
        char material{};
        if (g_engfuncs.pfnTraceTexture)
        {
            const char* texture = TRACE_TEXTURE(trace.pHit, start, end);
            if (!state->Current()) return false;
            if (texture)
            {
                if ((*texture == '+' || *texture == '-') && texture[1]) texture += 2;
                if (*texture == '{' || *texture == '!' || *texture == '~' || *texture == ' ') ++texture;
                std::array<char, 17> name{};
                std::strncpy(name.data(), texture, name.size() - 1);
                material = TEXTURETYPE_Find(name.data());
            }
        }
        unsigned family = 4;
        bool random{};
        switch (material)
        {
        case 'C': case 'D': case 'T': family = 4; random = true; break;
        case 'G': case 'M': case 'P': case 'V': family = 0; random = true; break;
        case 'W': family = 2; random = true; break;
        default: break;
        }
        // MP11487077/C2/DF consumes CRT rand, not engine RandomLong.
        sound = kick::HitSounds[family + (random ? unsigned(std::rand() % 2) : 0)];
    }
    player = state->Current();
    if (!player) return false;
    EMIT_SOUND_DYN(player->edict(), CHAN_ITEM, sound, 1.0f, 0.8f, 0, 100);
    return state->Current() != nullptr;
}

bool Attack(const std::shared_ptr<State>& state, ConfigError& error)
{
    auto* player = state->Current();
    const auto runtime = state->runtime.lock();
    if (!player || !runtime || !policyEnabled || !g_engfuncs.pfnTraceLine ||
        !g_engfuncs.pfnMakeVectors || !gpGlobals) return false;
    const auto session = runtime->session;
    const Vector origin = player->pev->origin;
    const Vector eyes = origin + player->pev->view_ofs;
    const Vector actorVelocity = player->pev->velocity;
    UTIL_MakeVectors(player->pev->v_angle);
    if (!state->Current()) return false;
    const Vector forward = gpGlobals->v_forward;
    if (!Finite(origin) || !Finite(eyes) || !Finite(forward) || !Finite(actorVelocity))
    { error.message = "human kick attack coordinates are invalid"; return false; }
    const Vector end = eyes + forward * kick::Reach;
    TraceResult obstacle{};
    UTIL_TraceLine(eyes, end, dont_ignore_monsters, player->edict(), &obstacle);
    if (!state->Current() || !policyEnabled) return false;
    struct Candidate { EHANDLE entity; std::uint64_t lease; Vector delta; };
    std::array<Candidate, CCSOLMonsterPool::Capacity> targets;
    std::size_t count{};
    // User AOE is bounded by the existing map-owned pool. Snapshot leases
    // before any damage callback: newly spawned/reused victims join a later kick.
    for (std::size_t slot = 0; slot < CCSOLMonsterPool::Capacity; ++slot)
    {
        auto* target = session->Monsters().At(slot);
        if (!target || target->Pool() != &session->Monsters() || !session->Monsters().Borrowed(*target) ||
            target->pev->deadflag != DEAD_NO || target->pev->health <= 0 ||
            target->pev->takedamage == DAMAGE_NO || (target->pev->flags & FL_KILLME)) continue;
        const Vector delta = target->pev->origin - origin;
        // User AOE: the forward half of the original short reach. The source
        // engine sphere uses entity bounds; reject a target behind the kicker.
        if (!Finite(delta) || DotProduct(delta, forward) < 0) continue;
        Vector nearest;
        if (!Finite(target->pev->absmin) || !Finite(target->pev->absmax)) continue;
        bool validBounds = true;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            if (target->pev->absmin[axis] > target->pev->absmax[axis]) { validBounds = false; break; }
            nearest[axis] = std::clamp(origin[axis], target->pev->absmin[axis], target->pev->absmax[axis]);
        }
        if (!validBounds || (nearest - origin).LengthSquared() > kick::Reach * kick::Reach) continue;
        targets[count++] = {EHANDLE(target), session->Monsters().Lease(*target), delta};
    }
    bool hit{};
    for (std::size_t i = 0; i < count; ++i)
    {
        player = state->Current();
        if (!player || !policyEnabled) return false;
        const auto& candidate = targets[i];
        auto* target = candidate.entity.Get<CCSOLScenarioMonster>();
        if (!target || target->Pool() != &session->Monsters() || !session->Monsters().Borrowed(*target) ||
            session->Monsters().Lease(*target) != candidate.lease || target->pev->deadflag != DEAD_NO ||
            target->pev->health <= 0) continue;
        Vector point;
        for (unsigned i = 0; i < 3; ++i)
            point[i] = std::clamp(eyes[i], target->pev->absmin[i], target->pev->absmax[i]);
        TraceResult trace{};
        UTIL_TraceLine(eyes, point, dont_ignore_monsters, player->edict(), &trace);
        player = state->Current();
        if (!player) return false;
        if (candidate.entity.Get<CBaseEntity>() != target || target->Pool() != &session->Monsters() ||
            !session->Monsters().Borrowed(*target) || session->Monsters().Lease(*target) != candidate.lease ||
            target->pev->deadflag != DEAD_NO || target->pev->health <= 0) continue;
        if (trace.flFraction < 1.0f && trace.pHit != candidate.entity.Get()) continue;
        const auto result = CSOL_HumanKickMonsterDamage(*target, *player, runtime,
            kick::BaseDamage, candidate.delta, actorVelocity, error,
            [](const void* value) { return policyEnabled && static_cast<const State*>(value)->Current() != nullptr; }, state.get());
        if (!state->Current() || !policyEnabled) return false;
        if (result == CSOLHumanKickDamageResult::Failed) return false;
        hit |= result == CSOLHumanKickDamageResult::Applied || result == CSOLHumanKickDamageResult::Killed;
    }
    return ImpactSound(state, eyes, end, obstacle, hit);
}
}

bool CSOL_PrecacheHumanKick(const std::shared_ptr<const CSOLScenarioEntityAccess>& access,
    ConfigError& error)
{
    if (preparing || !access || !access->Allowed() || !g_engfuncs.pfnGetFileSize ||
        !g_engfuncs.pfnPrecacheModel || !g_engfuncs.pfnPrecacheSound || !g_engfuncs.pfnRegUserMsg ||
        !g_engfuncs.pfnSzFromIndex || !g_engfuncs.pfnAllocString)
    { error.message = "human kick cold resources or engine API are unavailable"; return false; }
    Busy guard(preparing);
    const auto revision = ++resourceGeneration;
    resources = {}; states = {}; operating = {};
    CSOL_ResetActionStamina();
    const auto current = [&] { return access->Allowed() && resourceGeneration == revision; };
    const int size = g_engfuncs.pfnGetFileSize(kick::FootModel);
    if (!current()) return false;
    if (size <= 0) { error.message = "human kick foot model is missing"; return false; }
    for (const auto* sound : kick::HitSounds)
    {
        const std::string path = std::string("sound/") + sound;
        const int bytes = g_engfuncs.pfnGetFileSize(path.c_str());
        if (!current()) return false;
        if (bytes <= 0) { error.message = "human kick impact sound is missing: " + path; return false; }
    }
    const int soundSize = g_engfuncs.pfnGetFileSize("sound/zombi/z4/all/kick.wav");
    if (!current()) return false;
    if (soundSize <= 0) { error.message = "human kick swing sound is missing"; return false; }
    ColdResources candidate;
    candidate.access = access; candidate.generation = revision;
    candidate.model = PRECACHE_MODEL(kick::FootModel);
    if (!current()) return false;
    candidate.viewmodel = ALLOC_STRING(kick::FootModel);
    if (!current()) return false;
    PRECACHE_SOUND(kick::SwingSound);
    if (!current()) return false;
    for (const auto* sound : kick::HitSounds)
    { PRECACHE_SOUND(sound); if (!current()) return false; }
    candidate.message = REG_USER_MSG(kick::MessageName, static_cast<int>(kick::MessageSize));
    if (!current()) return false;
    if (!candidate.model || !candidate.viewmodel || candidate.message < 64 || candidate.message > 255)
    { error.message = "human kick resource registration failed"; return false; }
    resources = candidate;
    return true;
}

bool CSOL_UpdateHumanKick(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, ConfigError& error, bool force)
{
    const auto state = Prepare(runtime, player);
    if (!state || state->busy || operating[state->slot]) return true;
    Busy operation(operating[state->slot]);
    Busy guard(state->busy);
    if (player.pev->deadflag == DEAD_NO && player.pev->health > 0)
    {
        if (!CSOL_TickActionStamina(runtime, player, gpGlobals->time, error)) return false;
    }
    else CSOL_RetireActionStamina(player);
    if (state->clock.ActiveNow())
    {
        const bool alive = player.pev->deadflag == DEAD_NO && player.pev->health > 0;
        const bool sameWeapon = player.m_pActiveItem == state->weapon.Get<CBasePlayerItem>();
        if (!alive || !sameWeapon)
        { if (!Finish(state, false)) return false; }
        else if (!policyEnabled)
        { if (!Finish(state, true)) return false; }
        else if (player.pev->modelindex != state->activeModel)
        { if (!Finish(state, true)) return false; }
        else if (player.m_fSequenceFinished)
        { if (!Finish(state, true)) return false; }
    }
    if (!Publish(state, force))
    { if (state->Current()) error.message = "human kick state message failed"; return false; }
    return true;
}

bool CSOL_TryHumanKick(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, ConfigError& error)
{
    const auto state = Prepare(runtime, player);
    if (!state || state->busy || operating[state->slot]) return true;
    Busy operation(operating[state->slot]);
    Busy guard(state->busy);
    if (!CSOL_TickActionStamina(runtime, player, gpGlobals->time, error)) return false;
    if (!Ready(*state, player)) return true;
    auto* weapon = player.m_pActiveItem;
    if (!weapon || !state->clock.BeginEmergency(gpGlobals->time)) return true;
    state->weapon = weapon;
    state->action = {++nextAction};
    state->startedAt = gpGlobals->time; state->savedOwnerAttack = player.m_flNextAttack;
    state->savedViewmodel = player.pev->viewmodel; state->savedWeaponmodel = player.pev->weaponmodel;
    state->savedAnimation = player.pev->weaponanim;
    state->savedFov = player.pev->fov; state->savedIntFov = player.m_iFOV;
    if (auto* action = dynamic_cast<csol::scenario::custom::ResumableWeaponAction*>(weapon))
    {
        const auto result = action->SuspendWeaponAction(state->action);
        if (state->Current() != &player || state->weapon.Get<CBasePlayerItem>() != weapon ||
            player.m_pActiveItem != weapon) return false;
        if (result != csol::scenario::custom::ActionResumeResult::Applied)
        { state->clock.Finish(); state->action = {}; return true; }
        state->resumable = true;
    }
    const auto debit = CSOL_TryDebitKickStamina(runtime, player, state->action.generation);
    if (debit != CSOLKickStaminaDebit::Applied)
    {
        if (state->resumable) return Finish(state, true);
        state->clock.Finish(); state->action = {};
        return true;
    }
    state->activeModel = player.pev->modelindex;
    player.pev->sequence = state->sequence; player.pev->frame = 0;
    player.ResetSequenceInfo();
    if (state->Current() != &player) return false;
    player.m_flFrameRate = kick::PlayerFrameRate;
    player.m_flNextAttack = 1000000.0f; // Host CLIENT_WEAPONS duration, not a recovery timer.
    state->locked = true;
    player.pev->viewmodel = resources.viewmodel; player.pev->weaponmodel = 0;
    player.pev->fov = 90; player.m_iFOV = 90;
    player.pev->weaponanim = 1; player.pev->framerate = 1;
    if (!Animation(state, 1)) return false;
    if (!Publish(state, true)) return false;
    return Attack(state, error);
}

void CSOL_ResetHumanKick(CBasePlayer& player, bool restore)
{
    const auto state = Find(player);
    if (!state) return;
    restore = restore && state->clock.ActiveNow() && player.pev->deadflag == DEAD_NO &&
        player.pev->health > 0 && player.m_pActiveItem == state->weapon.Get<CBasePlayerItem>();
    // Finish guards restoration reentry. Keep the player's real weapon clocks
    // and in-flight reload; reset never calls its ordinary Deploy method.
    Finish(state, restore);
    state->retired = true;
}
bool CSOL_PreserveHumanKickAnimation(CBasePlayer& player, int activity)
{
    const auto state = Find(player);
    return state && state->clock.ActiveNow() && activity != PLAYER_DIE &&
        player.pev->deadflag == DEAD_NO && player.pev->health > 0;
}
void CSOL_SetHumanKickPolicy(bool enabled) { policyEnabled = enabled; }
