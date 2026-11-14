// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "action_stamina.h"
#include "gameplay_settings.h"
#include "../characters.h"
#include "../../scenario/custom/stamina_audio.hpp"
#include "../../scenario/custom/stamina_snapshot.hpp"
#include "../scenario/core/game_rules_gate.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "../scenario/core/scenario_player_runtime.h"
#include <array>
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
    custom::StaminaSnapshot published;
    std::uint32_t hudRevision{};
    bool hudPublished{};
    bool retired{};
    custom::LifeKey key;
    custom::StaminaState stamina;
    custom::StaminaConfig config;
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
    if (settings && settings->Enabled(custom::Feature::TimeCycle) && phaseOwner.available &&
        phaseOwner.runtime.lock() == runtime && phaseOwner.session.lock() == runtime->session &&
        runtime->Generation() == phaseOwner.generation && runtime->session->Map().EntityAccess() == phaseOwner.access &&
        phaseOwner.access && phaseOwner.access->Allowed() && runtime->session->Map().RoundGeneration() == phaseOwner.round)
        return phaseOwner.phase;
    return custom::Phase::Night;
}
bool Configuration(const custom::Snapshot& settings, custom::StaminaConfig& config)
{
    // Verified CoF ordinary profile defaults100/1/2/.25; original speed
    // references are percentages and are never written to host movement here.
    const auto apply = [&](custom::Setting key, float& target)
    {
        const auto value = settings.Number(key);
        if (!value) return true;
        if (!std::isfinite(*value) || *value > std::numeric_limits<float>::max()) return false;
        target = static_cast<float>(*value); return std::isfinite(target);
    };
    if (!apply(custom::Setting::SprintMaximum, config.maximum) ||
        !apply(custom::Setting::SprintRecoveryPerSecond, config.recovery) ||
        !apply(custom::Setting::KickCost, config.kickCost) ||
        !apply(custom::Setting::DuskMaximumMultiplier, config.duskMaximum) ||
        !apply(custom::Setting::DuskRecoveryMultiplier, config.duskRecovery)) return false;
    // Existing schema recovery is per-second, but original1/2 are per-cycle.
    // Preserve ordinary default1/crouch2 when absent; explicit per-second
    // overrides are converted once at this cold profile boundary.
    if (settings.Number(custom::Setting::SprintRecoveryPerSecond))
    { config.recovery *= .25f; config.duckRecovery = config.recovery * 2.0f; }
    return custom::Valid(config);
}
State* Find(const Runtime& runtime, CBasePlayer& player)
{
    for (auto& state : states)
        if (state.address == &player && state.Current(runtime, player)) return &state;
    return nullptr;
}
bool PublishHud(State& state,const Runtime& runtime,CBasePlayer& player,const custom::StaminaView& view,
    csol::scenario::ConfigError& error,bool force)
{
    const bool visible=CSOL_CustomFeatureEnabled(custom::Feature::Kick);
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
    for(const auto* sound:custom::RecoverySounds)
    {
        const auto path=std::string("sound/")+sound;
        const int bytes=g_engfuncs.pfnGetFileSize(path.c_str());
        if(!access->Allowed())return false;
        if(bytes<=0){error.message="stamina recovery sound is missing or empty: "+path;return false;}
        const auto index=PRECACHE_SOUND(sound);if(!access->Allowed())return false;
        if(index<=0){error.message="stamina recovery sound could not be registered";return false;}
    }
    breathAccess=access;breathMessage=message;staminaMessage=hud;return true;
}

bool CSOL_TickActionStamina(const Runtime& runtime, CBasePlayer& player, float now, csol::scenario::ConfigError& error,bool forceHud)
{
    if (operating) return true;
    Guard guard;
    if (!runtime || runtime->Closing() || !runtime->session || !runtime->players || !runtime->players->Active() ||
        runtime->session->State() != CSOLScenarioSessionState::Active || !std::isfinite(now)) return false;
    const auto access = runtime->session->Map().EntityAccess();
    if (!access || !access->Allowed()) return false;
    const int slot = player.entindex();
    if (slot < 1 || slot > 32 || !access->Allowed() || runtime->Closing()) return false;
    auto& state = states[slot];
    const auto settings = MapSettings(runtime, access);
    if (!settings) { error.message = "kick stamina settings are unavailable"; return false; }
    auto profile = runtime->players->Find(EHANDLE(&player));
    if (!profile || !access->Allowed() || runtime->Closing() || player.pev->deadflag != DEAD_NO ||
        player.pev->health <= 0 || player.has_disconnected) return false;
    const bool current = state.Current(runtime, player);
    // Retirement is a same-native-life tombstone, not a refill opportunity.
    if (!current && state.retired && state.address == &player && state.profile == profile &&
        state.life == profile->skillLifeSerial && state.spawn == profile->nativeSpawnSerial && state.nativeSpawns == player.m_iNumSpawns &&
        state.access == access && state.round == runtime->session->Map().RoundGeneration()) return false;
    custom::StaminaConfig config = current && state.settings == settings ? state.config : custom::StaminaConfig{};
    if ((!current || state.settings != settings) && !Configuration(*settings, config))
    { error.message = "kick stamina profile is invalid"; return false; }
    const auto phase = Phase(runtime, settings);
    if (!current)
    {
        if (serial == UINT64_MAX) { error.message = "kick stamina owner identity exhausted"; return false; }
        state = {}; state.runtime = runtime; state.session = runtime->session; state.players = runtime->players;
        state.access = access; state.profile = profile; state.player = &player; state.address = &player;
        state.runtimeGeneration = runtime->Generation(); state.round = runtime->session->Map().RoundGeneration();
        state.generation = profile->generation; state.life = profile->skillLifeSerial; state.spawn = profile->nativeSpawnSerial;
        state.nativeSpawns = player.m_iNumSpawns; state.serial = ++serial;
        state.key = {{state.runtimeGeneration, state.round}, state.serial};
        if (!state.stamina.Begin(state.key, phase, now, config)) return false;
    }
    if (!state.Current(runtime, player)) return false;
    if (!state.stamina.Configure(state.key, config, phase)) return false;
    state.settings = settings; state.config = config;
    if (state.tick == UINT64_MAX) { error.message = "kick stamina tick identity exhausted"; return false; }
    custom::StaminaInput input; input.key = state.key; input.command = ++state.tick; input.now = now; input.phase = phase;
    input.ducked = (player.pev->flags & FL_DUCKING) != 0;
    // No sprint or acceptedJump events: their real movement consumers remain
    // unintegrated. Recovery does not borrow weapon reload/aim eligibility.
    const auto before=state.stamina.View(state.key);
    const auto after=state.stamina.Step(input);
    if(!after||!state.Current(runtime,player))return false;
    if(before&&!before->exhausted&&after->exhausted&&!after->infinite)
    {
        bool male{};
        const bool resolved=CSOL_CharacterMale(&player,male,access);
        if(!state.Current(runtime,player))return false;
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
    return state.Current(runtime,player)&&PublishHud(state,runtime,player,*after,error,forceHud);
}
std::optional<custom::StaminaView> CSOL_ActionStaminaView(const Runtime& runtime, CBasePlayer& player)
{
    if (operating) return {};
    const auto* state = Find(runtime, player);
    return state && state->settings && CSOL_CustomFeatureEnabled(custom::Feature::Kick) ?
        state->stamina.View(state->key) : std::nullopt;
}
bool CSOL_ActionStaminaCanKick(const Runtime& runtime, CBasePlayer& player)
{ const auto view = CSOL_ActionStaminaView(runtime, player); return view && view->kickAvailable; }
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
        }
}
void CSOL_ResetActionStamina() { states = {}; phaseOwner = {}; mapPolicy = {};breathAccess.reset();breathMessage=staminaMessage=0; }
bool CSOL_SetChallengeStaminaPhase(const Runtime& runtime, custom::Phase phase, bool available)
{
    if (operating || !runtime || runtime->Closing() || !runtime->session ||
        runtime->session->State() != CSOLScenarioSessionState::Active || !runtime->session->Map().EntityAccessAllowed() ||
        phase == custom::Phase::Ended) return false;
    phaseOwner = {runtime, runtime->session, runtime->session->Map().EntityAccess(), runtime->Generation(),
        runtime->session->Map().RoundGeneration(), phase, available};
    return true;
}
