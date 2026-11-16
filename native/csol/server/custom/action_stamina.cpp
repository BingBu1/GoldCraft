// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "pm_defs.h"
#include "action_stamina.h"
#include "gameplay_settings.h"
#include "../characters.h"
#include "../../scenario/custom/stamina_audio.hpp"
#include "../../scenario/custom/stamina_snapshot.hpp"
#include "../../scenario/custom/standalone_stamina_state.hpp"
#include "../scenario/core/game_rules_gate.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../scenario/core/scenario_player_runtime.h"
#include "../scenario/core/scenario_entity_lookup.h"
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace
{
namespace custom = csol::scenario::custom;
using Runtime = std::shared_ptr<CCSOLScenarioRuntime>;
struct State
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::weak_ptr<CCSOLScenarioSession> session;
    std::weak_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> profile;
    custom::SnapshotPtr settings;
    EHANDLE player;
    CBasePlayer* address{};
    std::uint64_t runtimeGeneration{}, round{}, generation{}, life{}, spawn{}, serial{}, tick{}, token{};
    int nativeSpawns{};
    std::uint32_t recoveryEvents{};
    std::uint64_t jumpRequest{},jumpConsumed{};
    custom::StaminaSnapshot published;
    std::uint32_t hudRevision{};
    bool hudPublished{};
    std::uint32_t inputRevision{};
    bool frameMovement{}, inputPublished{};
    bool retired{};
    custom::LifeKey key;
    custom::StandaloneStaminaState stamina;
    custom::StandaloneStaminaConfig config;
    bool Current(const Runtime& owner, CBasePlayer& actor, bool alive=true) const
    {
        if (retired || address != &actor || !access || !access->Allowed() || !owner ||
            owner->Closing() || owner != runtime.lock() || owner->Generation() != runtimeGeneration ||
            owner->session != session.lock() || owner->players != players.lock() ||
            !owner->session || owner->session->State() != CSOLScenarioSessionState::Active ||
            !owner->players || !owner->players->Active() ||
            owner->session->Map().EntityAccess() != access || owner->session->Map().RoundGeneration() != round ||
            !profile || owner->players->Find(player) != profile || profile->generation != generation ||
            profile->skillLifeSerial != life || profile->nativeSpawnSerial != spawn) return false;
        // Standard EHANDLE reads may enter an engine observer. Recheck pure
        // authority/retirement after returning before touching native fields.
        const auto expectedSerial = serial;
        if (player.Get<CBasePlayer>() != &actor || serial != expectedSerial || retired || !access || !access->Allowed() || owner->Closing()) return false;
        return actor.pev && !actor.has_disconnected && actor.m_iNumSpawns == nativeSpawns &&
            (!alive || (actor.pev->deadflag == DEAD_NO && actor.pev->health > 0)) &&
            !(actor.pev->flags & (FL_KILLME | FL_DORMANT));
    }
};
std::array<State, 33> states;
std::uint64_t serial{};
bool operating{};
int breathMessage{};
int staminaMessage{};
int inputMessage{};
std::shared_ptr<const CSOLScenarioEntityAccess> breathAccess;
struct Guard { Guard(){operating=true;} ~Guard(){operating=false;} };
struct PhaseOwner
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::uint64_t generation{}, round{};
    custom::Phase phase{custom::Phase::Night};
    bool available{};
} phaseOwner;
struct MapPolicy
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    custom::SnapshotPtr settings;
    std::uint64_t generation{};
} mapPolicy;
struct FrameOwner
{
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    std::uint64_t generation{}, round{};
    float time{};
    bool begun{}, completed{};
} frameOwner;
custom::SnapshotPtr MapSettings(const Runtime& runtime,
    const std::shared_ptr<const CSOLScenarioEntityAccess>& access)
{
    if (mapPolicy.runtime.lock() == runtime && mapPolicy.session.lock() == runtime->session &&
        mapPolicy.access == access && mapPolicy.generation == runtime->Generation()) return mapPolicy.settings;
    const auto settings = CSOL_CustomSettings();
    if (!settings) return {};
    // Numeric stamina policy is NextMapSession. A saved edit cannot change
    // capacity/cost halfway through the current map or its intermediate stage.
    mapPolicy = {runtime, runtime->session, access, settings, runtime->Generation()};
    return settings;
}
custom::Phase Phase(const Runtime& runtime, const custom::SnapshotPtr& settings)
{
    if (settings && phaseOwner.available &&
        phaseOwner.runtime.lock() == runtime && phaseOwner.session.lock() == runtime->session &&
        runtime->Generation() == phaseOwner.generation && runtime->session->Map().EntityAccess() == phaseOwner.access &&
        phaseOwner.access && phaseOwner.access->Allowed() && runtime->session->Map().RoundGeneration() == phaseOwner.round)
        return phaseOwner.phase;
    return custom::Phase::Night;
}
bool Configuration(const custom::Snapshot& settings, custom::StandaloneStaminaConfig& config)
{
    // Original CSOL StandAlone recovery108593D0: default100, delay3,
    // acceleration0.1. Host cost/phase deviations remain separately configured.
    const auto apply = [&](custom::Setting key, float& target)
    {
        const auto value = settings.Number(key);
        if (!value) return true;
        if (!std::isfinite(*value) || *value > std::numeric_limits<float>::max()) return false;
        target = static_cast<float>(*value); return std::isfinite(target);
    };
    if (!apply(custom::Setting::SprintMaximum, config.recovery.maximum) ||
        !apply(custom::Setting::SprintCostPerUnit, config.runCostPerUnit) ||
        !apply(custom::Setting::JumpCost, config.jumpCost) ||
        !apply(custom::Setting::KickCost, config.kickCost) ||
        !apply(custom::Setting::DuskMaximumMultiplier, config.duskMaximum) ||
        !apply(custom::Setting::DuskRecoveryMultiplier, config.duskRecovery)) return false;
    if (const auto recovery = settings.Number(custom::Setting::SprintRecoveryPerSecond))
    {
        if (!std::isfinite(*recovery) || *recovery > std::numeric_limits<float>::max()) return false;
        config.recoveryPerSecond = static_cast<float>(*recovery);
    }
    return custom::Valid(config);
}
State* Find(const Runtime& runtime, CBasePlayer& player)
{
    for (auto& state : states)
        if (state.address == &player && state.Current(runtime, player)) return &state;
    return nullptr;
}
bool Enabled()
{return CSOL_CustomFeatureEnabled(custom::Feature::Kick)||CSOL_CustomFeatureEnabled(custom::Feature::CofSprint);}
State* Prepare(const Runtime& runtime,CBasePlayer& player,float now,csol::scenario::ConfigError& error)
{
    if(!runtime||runtime->Closing()||!runtime->session||!runtime->players||!runtime->players->Active()||
        runtime->session->State()!=CSOLScenarioSessionState::Active||!std::isfinite(now))return nullptr;
    const auto access=runtime->session->Map().EntityAccess();
    if(!access||!access->Allowed())return nullptr;
    const int slot=player.entindex();
    if(slot<1||slot>32||!access->Allowed()||runtime->Closing())return nullptr;
    auto& state=states[slot];
    const auto settings=MapSettings(runtime,access);
    if(!settings){error.message="kick stamina settings are unavailable";return nullptr;}
    auto profile=runtime->players->Find(EHANDLE(&player));
    if(!profile||!access->Allowed()||runtime->Closing()||!player.pev||player.pev->deadflag!=DEAD_NO||
        player.pev->health<=0||player.has_disconnected)return nullptr;
    const bool current=state.Current(runtime,player);
    // A retired native life stays retired until an actual new life is prepared.
    if(!current&&state.retired&&state.address==&player&&state.profile==profile&&
        state.life==profile->skillLifeSerial&&state.spawn==profile->nativeSpawnSerial&&
        state.nativeSpawns==player.m_iNumSpawns&&state.access==access&&state.round==runtime->session->Map().RoundGeneration())return nullptr;
    custom::StandaloneStaminaConfig config=current&&state.settings==settings?state.config:custom::StandaloneStaminaConfig{};
    if((!current||state.settings!=settings)&&!Configuration(*settings,config))
    {error.message="kick stamina profile is invalid";return nullptr;}
    const auto phase=Phase(runtime,settings);
    if(!current)
    {
        if(serial==UINT64_MAX){error.message="kick stamina owner identity exhausted";return nullptr;}
        state={};state.runtime=runtime;state.session=runtime->session;state.players=runtime->players;
        state.access=access;state.profile=profile;state.player=&player;state.address=&player;
        state.runtimeGeneration=runtime->Generation();state.round=runtime->session->Map().RoundGeneration();
        state.generation=profile->generation;state.life=profile->skillLifeSerial;state.spawn=profile->nativeSpawnSerial;
        state.nativeSpawns=player.m_iNumSpawns;state.serial=++serial;
        state.key={{state.runtimeGeneration,state.round},state.serial};
        if(!state.stamina.Begin(state.key,phase,now,config))return nullptr;
    }
    if(!state.Current(runtime,player)||!state.stamina.Configure(state.key,config,phase))return nullptr;
    state.settings=settings;state.config=config;return &state;
}
bool PublishHud(State& state,const Runtime& runtime,CBasePlayer& player,const custom::StaminaView& view,
    csol::scenario::ConfigError& error,bool force)
{
    const bool visible=Enabled();
    custom::StaminaSnapshot next{state.serial,0,visible?view.amount:0,visible?view.maximum:0,
        visible,visible&&view.infinite,visible&&view.exhausted};
    const auto& old=state.published;
    if(!force&&state.hudPublished&&next.amount==old.amount&&next.maximum==old.maximum&&next.visible==old.visible&&
        next.infinite==old.infinite&&next.exhausted==old.exhausted)return state.Current(runtime,player);
    if(state.hudRevision==UINT32_MAX||staminaMessage<64||breathAccess!=state.access||
        !g_engfuncs.pfnMessageBegin||!g_engfuncs.pfnWriteByte||!g_engfuncs.pfnMessageEnd)
    {error.message="stamina HUD has no prepared current transport";return false;}
    next.revision=++state.hudRevision;
    const auto bytes=custom::EncodeStamina(next);
    if(!bytes){error.message="stamina HUD snapshot is invalid";return false;}
    if(!state.Current(runtime,player))return false;
    state.published=next;state.hudPublished=true;
    MESSAGE_BEGIN(MSG_ONE,staminaMessage,nullptr,player.pev);
    for(const auto byte:*bytes)WRITE_BYTE(std::to_integer<unsigned>(byte));
    MESSAGE_END();
    return state.Current(runtime,player);
}
bool PublishInput(State& state, const Runtime& runtime, CBasePlayer& player,
    csol::scenario::ConfigError& error)
{
    if (state.inputPublished) return state.Current(runtime, player);
    if (state.inputRevision == UINT32_MAX || inputMessage < 64 || breathAccess != state.access ||
        !g_engfuncs.pfnMessageBegin || !g_engfuncs.pfnWriteByte || !g_engfuncs.pfnMessageEnd)
    { error.message = "standalone input has no prepared current transport"; return false; }
    const auto serial = state.serial;
    const auto bytes = custom::EncodeStandaloneInput({serial, ++state.inputRevision, true});
    if (!bytes || !state.Current(runtime, player) || state.serial != serial) return false;
    state.inputPublished = true;
    MESSAGE_BEGIN(MSG_ONE, inputMessage, nullptr, player.pev);
    for (const auto byte : *bytes) WRITE_BYTE(std::to_integer<unsigned>(byte));
    MESSAGE_END();
    return state.Current(runtime, player) && state.serial == serial;
}
}

bool CSOL_PrecacheActionStamina(const std::shared_ptr<const CSOLScenarioEntityAccess>& access,
    csol::scenario::ConfigError& error)
{
    error={};
    if(!access||!access->Allowed()||!g_engfuncs.pfnRegUserMsg||!g_engfuncs.pfnGetFileSize||
        !g_engfuncs.pfnPrecacheSound)
    {error.message="stamina recovery sound cold APIs are unavailable";return false;}
    const int message=REG_USER_MSG(custom::BreathMessage,static_cast<int>(custom::BreathMessageSize));
    if(!access->Allowed())return false;
    if(message<64||message>255){error.message="stamina recovery message registration failed";return false;}
    const int hud=REG_USER_MSG(custom::StaminaMessage,static_cast<int>(custom::StaminaMessageSize));
    if(!access->Allowed())return false;
    if(hud<64||hud>255){error.message="stamina HUD message registration failed";return false;}
    const int input = REG_USER_MSG(custom::StandaloneInputMessage, static_cast<int>(custom::StandaloneInputMessageSize));
    if (!access->Allowed()) return false;
    if (input < 64 || input > 255) { error.message = "standalone input message registration failed"; return false; }
    for(const auto* sound:custom::RecoverySounds)
    {
        const auto path=std::string("sound/")+sound;
        const int bytes=g_engfuncs.pfnGetFileSize(path.c_str());
        if(!access->Allowed())return false;
        if(bytes<=0){error.message="stamina recovery sound is missing or empty: "+path;return false;}
        const auto index=PRECACHE_SOUND(sound);if(!access->Allowed())return false;
        if(index<=0){error.message="stamina recovery sound could not be registered";return false;}
    }
    breathAccess=access;breathMessage=message;staminaMessage=hud;inputMessage=input;return true;
}

namespace
{
bool AdvanceRecovery(State& state, const Runtime& runtime, CBasePlayer& player, float now,
    csol::scenario::ConfigError& error, bool forceHud)
{
    const auto access = state.access;
    const auto serial = state.serial;
    if (state.tick == UINT64_MAX) { error.message = "kick stamina tick identity exhausted"; return false; }
    const auto after=state.stamina.Step(state.key,++state.tick,now,gpGlobals?gpGlobals->frametime:0);
    if(!after||!state.Current(runtime,player)||state.serial!=serial)return false;
    if(state.stamina.RecoveryVoice()&&!after->infinite)
    {
        bool male{};
        const bool resolved=CSOL_CharacterMale(&player,male,access);
        if(!state.Current(runtime,player)||state.serial!=serial)return false;
        if(resolved)
        {
        if(breathAccess!=access||breathMessage<64||state.recoveryEvents==UINT32_MAX||
            !g_engfuncs.pfnMessageBegin||!g_engfuncs.pfnWriteByte||!g_engfuncs.pfnMessageEnd)
        {error.message="stamina recovery sound has no prepared current transport";return false;}
        const auto bytes=custom::EncodeRecoverySound({male,state.serial,++state.recoveryEvents});
        if(!bytes)return false;
        MESSAGE_BEGIN(MSG_ONE,breathMessage,nullptr,player.pev);
        for(const auto byte:*bytes)WRITE_BYTE(std::to_integer<unsigned>(byte));
        MESSAGE_END();
        }
    }
    return state.Current(runtime,player)&&state.serial==serial&&PublishHud(state,runtime,player,*after,error,forceHud);
}
}
bool CSOL_TickActionStamina(const Runtime& runtime, CBasePlayer& player, float now, csol::scenario::ConfigError& error,bool forceHud)
{
    if (operating) return true;
    Guard guard;
    auto* state = Prepare(runtime, player, now, error);
    if (!state) return false;
    if (!state->frameMovement) return AdvanceRecovery(*state, runtime, player, now, error, forceHud);
    const auto view = state->stamina.View(state->key);
    return view && PublishHud(*state, runtime, player, *view, error, forceHud);
}
bool CSOL_TickStandaloneMovementFrame(const Runtime& runtime, csol::scenario::ConfigError& error)
{
    error = {};
    if (operating || !runtime || runtime->Closing() || !runtime->session || !runtime->players || !gpGlobals ||
        runtime->session->State() != CSOLScenarioSessionState::Active || !runtime->players->Active()) return false;
    Guard guard;
    const auto access = runtime->session->Map().EntityAccess();
    if (!access || !access->Allowed()) return false;
    const auto settings = MapSettings(runtime, access);
    if (!settings) return false;
    if (!settings->Enabled(custom::Feature::CofSprint)) return true;
    const auto generation = runtime->Generation(), round = runtime->session->Map().RoundGeneration();
    const auto session = runtime->session;
    const auto players = runtime->players;
    const float now = gpGlobals->time;
    const auto current = [&]
    {
        return !runtime->Closing() && access->Allowed() && runtime->Generation() == generation && runtime->session == session &&
            session->State() == CSOLScenarioSessionState::Active && runtime->players == players && players->Active() &&
            runtime->session->Map().EntityAccess() == access && runtime->session->Map().RoundGeneration() == round;
    };
    if (!std::isfinite(now) || !std::isfinite(gpGlobals->frametime) || gpGlobals->frametime < 0 ||
        gpGlobals->maxClients < 1 || gpGlobals->maxClients > 32)
    { error.message = "standalone frame clock or player bound is invalid"; return false; }
    if (frameOwner.begun && frameOwner.runtime.lock() == runtime && frameOwner.access == access &&
        frameOwner.generation == generation && frameOwner.round == round)
    {
        if (!frameOwner.completed)
        { error.message = "standalone frame did not complete; its owner must be retired"; return false; }
        if (frameOwner.time == now) return current();
        if (frameOwner.time > now) { error.message = "standalone frame clock moved backwards"; return false; }
    }
    frameOwner = {runtime, access, generation, round, now, true, false};
    for (int slot = 1; slot <= gpGlobals->maxClients; ++slot)
    {
        auto* player = dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(slot, access));
        if (!current()) return false;
        if (!player || player->has_disconnected || !player->pev || player->m_iTeam == SPECTATOR ||
            player->m_iTeam == UNASSIGNED || player->m_iJoiningState != JOINED) continue;
        if (player->pev->deadflag != DEAD_NO || player->pev->health <= 0)
        { CSOL_RetireActionStamina(*player); if (!current()) return false; continue; }
        auto* state = Prepare(runtime, *player, now, error);
        if (!current()) return false;
        if (!state) { if (!error.message.empty()) return false; continue; }
        const auto serial = state->serial;
        const auto valid = [&] { return current() && state->Current(runtime, *player) && state->serial == serial; };
        state->frameMovement = true;
        if (!state->stamina.RunDisplacement(state->key,
            {player->pev->origin.x, player->pev->origin.y, player->pev->origin.z}, (player->pev->flags & FL_DUCKING) != 0))
        { error.message = "standalone displacement update is invalid"; return false; }
        // Original108595D0 resets native speed before108593D0 recovery. The
        // normal ReAPI/native freeze, observer and scenario layers still run.
        player->ResetMaxSpeed();
        if (!valid() || !AdvanceRecovery(*state, runtime, *player, now, error, false) || !valid() ||
            !PublishInput(*state, runtime, *player, error) || !valid()) return false;
    }
    if (!current() || !frameOwner.begun || frameOwner.runtime.lock() != runtime || frameOwner.access != access ||
        frameOwner.generation != generation || frameOwner.round != round || frameOwner.time != now) return false;
    frameOwner.completed = true;
    return true;
}
bool CSOL_ActionStaminaRunCommand(const Runtime& runtime, CBasePlayer& player, bool requested, std::string_view life)
{
    if (operating || life.empty()) return false;
    std::uint64_t token{};
    const auto parsed = std::from_chars(life.data(), life.data() + life.size(), token);
    if (parsed.ec != std::errc{} || parsed.ptr != life.data() + life.size() || !token) return false;
    Guard guard;
    auto* state = Find(runtime, player);
    return state && state->frameMovement && state->inputPublished && state->serial == token &&
        state->stamina.RequestRun(state->key, requested);
}
std::optional<float> CSOL_ActionStaminaRunSpeed(const Runtime& runtime, CBasePlayer& player, float weaponSpeed)
{
    // Read-only during native ResetMaxSpeed, including our own frame callback.
    // Ordinary/kick-only maps never prepare a movement frame; avoid resolving
    // player/session/EHANDLE ownership on every ordinary weapon speed reset.
    if (!frameOwner.begun) return {};
    auto* state = Find(runtime, player);
    if (!state || !state->frameMovement || !player.pev || !std::isfinite(weaponSpeed) || !g_engfuncs.pfnCVarGetFloat) return {};
    const auto serial = state->serial;
    const bool ducking = (player.pev->flags & FL_DUCKING) != 0;
    const bool running = state->stamina.Running(state->key);
    const float speed = custom::StandaloneMaxSpeed(weaponSpeed, ducking, running,
        [] { return CVAR_GET_FLOAT("sv_maxspeed"); });
    if (!std::isfinite(speed) || !state->Current(runtime, player) || state->serial != serial) return {};
    return speed;
}
std::optional<custom::StaminaView> CSOL_ActionStaminaView(const Runtime& runtime, CBasePlayer& player)
{
    if (operating) return {};
    const auto* state = Find(runtime, player);
    return state && state->settings && Enabled() ?
        state->stamina.View(state->key) : std::nullopt;
}
bool CSOL_ActionStaminaCanKick(const Runtime& runtime, CBasePlayer& player)
{ const auto view = CSOL_ActionStaminaView(runtime, player); return view && view->kickAvailable; }
CSOLActionStaminaJump CSOL_PrepareActionStaminaJump(const playermove_s& movement)
{
    if(!movement.server||movement.movetype!=MOVETYPE_WALK||!Enabled())return {};
    const CSOLActionStaminaJump denied{0,0,0,true};
    if(operating||movement.player_index<0||movement.player_index>=32||!gpGlobals)return denied;
    Guard guard;CSOLScenarioSdkScope scope;
    if(!scope.Scenario())return {};
    if(!scope.Current())return denied;
    const auto access=scope.EntityAccess();const auto runtime=CSOL_ScenarioRuntimeForAccess(access.get());
    auto* player=dynamic_cast<CBasePlayer*>(CSOL_ScenarioPlayerEntity(movement.player_index+1,access));
    if(!player||!scope.Current())return denied;
    csol::scenario::ConfigError error;auto* state=Prepare(runtime,*player,gpGlobals->time,error);
    if(!state||!scope.Current()||state->jumpRequest==UINT64_MAX)return denied;
    return {state->serial,++state->jumpRequest,unsigned(movement.player_index+1)};
}
bool CSOL_ActionStaminaJumpAllowed(CSOLActionStaminaJump jump)
{
    if(jump.denied)return false;
    if(!jump.owner)return true;
    if(operating||!jump.request||jump.slot<1||jump.slot>=states.size())return false;
    Guard guard;auto& state=states[jump.slot];
    if(state.serial!=jump.owner||state.retired||jump.request>state.jumpRequest||jump.request<=state.jumpConsumed)return false;
    const auto runtime=state.runtime.lock();auto* player=state.address;
    return player&&state.Current(runtime,*player)&&state.serial==jump.owner&&state.stamina.CanJump(state.key);
}
bool CSOL_ActionStaminaJumpBlocked(CBasePlayer& player)
{
    if(!Enabled())return false;
    if(operating)return true;
    Guard guard;CSOLScenarioSdkScope scope;
    if(!scope.Scenario())return false;
    if(!scope.Current())return true;
    const auto runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
    const auto* state=Find(runtime,player);
    return !state||!state->stamina.CanJump(state->key);
}
bool CSOL_CommitActionStaminaJump(CSOLActionStaminaJump jump)
{
    if(operating||!jump.owner||!jump.request||jump.slot<1||jump.slot>=states.size())return false;
    Guard guard;auto& state=states[jump.slot];
    if(state.serial!=jump.owner||state.retired||jump.request>state.jumpRequest||jump.request<=state.jumpConsumed)return false;
    const auto runtime=state.runtime.lock();auto* player=state.address;
    if(!player||!state.Current(runtime,*player)||state.serial!=jump.owner)return false;
    state.jumpConsumed=jump.request;
    return Enabled()&&state.stamina.DebitJump(state.key);
}
CSOLKickStaminaDebit CSOL_TryDebitKickStamina(const Runtime& runtime, CBasePlayer& player, std::uint64_t token)
{
    if (operating) return CSOLKickStaminaDebit::Declined;
    Guard guard;
    auto* state = Find(runtime, player);
    if (!state) return CSOLKickStaminaDebit::Retired;
    if (!token || !state->settings || !CSOL_CustomFeatureEnabled(custom::Feature::Kick)) return CSOLKickStaminaDebit::Declined;
    if (token == state->token) return CSOLKickStaminaDebit::Duplicate;
    if (token < state->token || !state->stamina.DebitKick(state->key)) return CSOLKickStaminaDebit::Declined;
    state->token = token; return CSOLKickStaminaDebit::Applied;
}
bool CSOL_DebitGuestCofPushbackStamina(const Runtime& runtime, CBasePlayer& player, float requestedAmount)
{
    if (operating || !std::isfinite(requestedAmount) || !Enabled()) return false;
    Guard guard;
    auto* state = Find(runtime, player);
    if (!state || !state->settings) return false;
    const auto view = state->stamina.View(state->key);
    return view && requestedAmount == view->amount - 15.F &&
        state->stamina.DebitExternalAction(state->key, view->amount, 15.F);
}
bool CSOL_ReadGuestCofPushbackStamina(const Runtime& runtime, CBasePlayer& player, float& amount, bool& infinite)
{
    const auto view=CSOL_ActionStaminaView(runtime,player);
    if(!view)return false;
    amount=view->infinite?view->maximum:view->amount;
    infinite=view->infinite;
    return true;
}
void CSOL_RetireActionStamina(CBasePlayer& player)
{
    for (auto& state : states)
        if (state.address == &player)
        {
            if(state.retired)continue;
            const auto retiring=state;
            state.retired=true;state.stamina.Retire(state.key);
            const auto runtime=retiring.runtime.lock();
            const auto access=retiring.access;
            std::optional<std::array<std::byte,custom::StaminaMessageSize>> bytes;
            if(retiring.hudPublished&&retiring.published.visible&&retiring.hudRevision<UINT32_MAX&&
                breathAccess==access&&staminaMessage>=64&&retiring.Current(runtime,player,false)&&
                state.serial==retiring.serial&&state.retired)
            {
                state.published={state.serial,++state.hudRevision,0,0,false,false,false};
                bytes=custom::EncodeStamina(state.published);
            }
            if(bytes&&runtime&&!runtime->Closing()&&access&&access->Allowed()&&
                g_engfuncs.pfnMessageBegin&&g_engfuncs.pfnWriteByte&&g_engfuncs.pfnMessageEnd)
            {
                MESSAGE_BEGIN(MSG_ONE,staminaMessage,nullptr,player.pev);
                for(const auto byte:*bytes)WRITE_BYTE(std::to_integer<unsigned>(byte));
                MESSAGE_END();
            }
            if (retiring.inputPublished && retiring.inputRevision < UINT32_MAX && breathAccess == access && inputMessage >= 64 &&
                retiring.Current(runtime, player, false) && state.serial == retiring.serial && state.retired &&
                g_engfuncs.pfnMessageBegin && g_engfuncs.pfnWriteByte && g_engfuncs.pfnMessageEnd)
            {
                const auto input = custom::EncodeStandaloneInput({retiring.serial, ++state.inputRevision, false});
                if (input)
                {
                    MESSAGE_BEGIN(MSG_ONE, inputMessage, nullptr, player.pev);
                    for (const auto byte : *input) WRITE_BYTE(std::to_integer<unsigned>(byte));
                    MESSAGE_END();
                }
            }
        }
}
void CSOL_ResetActionStamina() { states = {}; phaseOwner = {}; mapPolicy = {};frameOwner={};breathAccess.reset();breathMessage=staminaMessage=inputMessage=0; }
bool CSOL_SetChallengeStaminaPhase(const Runtime& runtime, custom::Phase phase, bool available)
{
    if (operating || !runtime || runtime->Closing() || !runtime->session ||
        runtime->session->State() != CSOLScenarioSessionState::Active || !runtime->session->Map().EntityAccessAllowed() ||
        phase == custom::Phase::Ended) return false;
    phaseOwner = {runtime, runtime->session, runtime->session->Map().EntityAccess(), runtime->Generation(),
        runtime->session->Map().RoundGeneration(), phase, available};
    return true;
}
