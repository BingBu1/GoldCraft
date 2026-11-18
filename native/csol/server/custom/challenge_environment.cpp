// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "challenge_environment.h"
#include "../../scenario/custom/night_flashlight.hpp"
#include "gameplay_settings.h"
#include "../scenario/core/scenario_runtime.h"
#include "../scenario/core/scenario_session.h"
#include "goldcraft/lightstyle_api.hpp"

namespace
{
std::uint64_t environmentSerial{};
using Runtime = std::shared_ptr<CCSOLScenarioRuntime>;
bool Current(const Runtime& runtime, std::uint64_t generation)
{
    if (!runtime || runtime->Closing() || runtime->Generation() != generation || !runtime->session ||
        runtime->session->State() != CSOLScenarioSessionState::Active) return false;
    const auto& access = runtime->session->Map().EntityAccess();
    return access && access->Allowed();
}
void ClearFlashlightModels(CBasePlayer& player, string_t view, string_t held)
{
    if (player.pev->viewmodel == view) player.pev->viewmodel = 0;
    if (player.pev->weaponmodel == held) player.pev->weaponmodel = 0;
}
struct Busy
{
    bool& flag;
    explicit Busy(bool& value) : flag(value) { flag = true; }
    ~Busy() { flag = false; }
};
}

bool CCSOLChallengeEnvironment::Precache(const csol::scenario::custom::Snapshot& settings)
{
    namespace custom = csol::scenario::custom;
    if (prepared_ || busy_ || revoked_) return false;
    const auto config = custom::ReadEnvironmentConfig(settings);
    const auto night = custom::ReadNightLighting(settings);
    if (!config || !night) return false;
    const auto factory = Sys_GetFactory("swds.dll");
    lights_ = factory ? static_cast<goldcraft::IMapLightStyleService*>(
        factory(goldcraft::MapLightStyleServiceVersion, nullptr)) : nullptr;
    if (!lights_) return false;
    message_ = REG_USER_MSG(custom::EnvironmentMessage, static_cast<int>(custom::EnvironmentMessageSize));
    nightMessage_ = REG_USER_MSG(custom::NightLightingMessage, static_cast<int>(custom::NightLightingMessageSize));
    if (revoked_ || message_ < 64 || message_ > 255 || nightMessage_ < 64 || nightMessage_ > 255) return false;
    if (g_engfuncs.pfnGetFileSize && GET_FILE_SIZE(custom::NightFlashlightView) > 0 &&
        GET_FILE_SIZE(custom::NightFlashlightPlayer) > 0)
    {
        if (revoked_) return false;
        if (PRECACHE_MODEL(custom::NightFlashlightView) > 0 && !revoked_ &&
            PRECACHE_MODEL(custom::NightFlashlightPlayer) > 0 && !revoked_)
        {
            flashlightView_ = ALLOC_STRING(custom::NightFlashlightView);
            flashlightPlayer_ = ALLOC_STRING(custom::NightFlashlightPlayer);
        }
    }
    if (revoked_) return false;
    night_.config = *night;
    config_ = *config; prepared_ = true; return true;
}
bool CCSOLChallengeEnvironment::Update(const Runtime& runtime, std::uint64_t challenge,
    csol::scenario::custom::Phase phase, float now)
{
    namespace custom = csol::scenario::custom;
    if (!prepared_ || revoked_ || busy_ || !runtime) return false;
    const auto generation = runtime->Generation();
    if (!Current(runtime, generation)) return false;
    Busy busy(busy_);
    if (challenge_ != challenge)
    {
        if ((challenge_ && (runtime_.lock() != runtime || challenge <= challenge_)) ||
            environmentSerial == UINT64_MAX) return false;
        if (lightOwner_)
        {
            const auto token = lightOwner_; lightOwner_ = 0;
            if (!lights_->Release(token) || revoked_ || !Current(runtime, generation)) return false;
        }
        if (!fade_.Begin(challenge, config_)) return false;
        if (NightFlashlightsAvailable()) ClearNightFlashlights(true);
        if (revoked_ || !Current(runtime, generation)) return false;
        challenge_ = challenge; runtime_ = runtime;
        snapshot_ = {}; snapshot_.owner = ++environmentSerial; snapshot_.profile = config_.fog;
        night_.owner = snapshot_.owner; nightKnown_ = false;
        known_ = false;
    }
    else if (runtime_.lock() != runtime) return false;
    const auto value = fade_.Advance(challenge, phase, now);
    if (!value) return false;
    // Read registered numeric CVARs, including temporary console overrides.
    // No JSON parsing, string lookup or player/light list runs on this path.
    auto config = night_.config;
    const bool configChanged = CSOL_ReadNightLightingControls(config) && night_.config != config;
    if (configChanged) night_.config = config;
    if (!nightKnown_ || night_.phase != phase || configChanged)
    {
        const bool leavingNight = phase != custom::Phase::Night && NightFlashlightsAvailable();
        night_.phase = phase; nightKnown_ = true;
        if (leavingNight) ClearNightFlashlights(true);
        if (revoked_ || !Current(runtime, generation)) return false;
        if (!PublishNight(runtime, nullptr)) return false;
    }
    if (value->active)
    {
        if (!lightOwner_)
        {
            lightOwner_ = lights_->Acquire();
            if (!lightOwner_ || revoked_ || !Current(runtime, generation)) return false;
        }
        if ((!known_ || snapshot_.view != *value) &&
            (!lights_->Set(lightOwner_, value->level) || revoked_ || !Current(runtime, generation))) return false;
    }
    else if (lightOwner_)
    {
        const auto token = lightOwner_; lightOwner_ = 0;
        if (!lights_->Release(token) || revoked_ || !Current(runtime, generation)) return false;
    }
    if (known_ && snapshot_.view == *value) return true;
    snapshot_.view = *value; known_ = true;
    return Publish(runtime, nullptr);
}
bool CCSOLChallengeEnvironment::Publish(const Runtime& runtime, CBasePlayer* player)
{
    namespace custom = csol::scenario::custom;
    if (revoked_ || !known_ || runtime_.lock() != runtime || revision_ == UINT32_MAX) return false;
    const auto generation = runtime->Generation();
    if (!Current(runtime, generation)) return false;
    const auto session = runtime->session;
    const auto round = session->Map().RoundGeneration();
    const EHANDLE recipient(player);
    if (player && (player->has_disconnected || !player->pev ||
        (player->pev->flags & (FL_FAKECLIENT | FL_KILLME | FL_DORMANT)))) return true;
    snapshot_.revision = ++revision_;
    const auto bytes = custom::EncodeEnvironment(snapshot_);
    if (!bytes) return false;
    if (player) MESSAGE_BEGIN(MSG_ONE, message_, nullptr, player->pev);
    else MESSAGE_BEGIN(MSG_ALL, message_);
    for (const auto byte : *bytes) WRITE_BYTE(std::to_integer<unsigned>(byte));
    MESSAGE_END();
    return !revoked_ && Current(runtime, generation) && runtime->session == session &&
        session->Map().RoundGeneration() == round && (!player || recipient.Get<CBasePlayer>() == player);
}
bool CCSOLChallengeEnvironment::Replay(const Runtime& runtime, CBasePlayer& player)
{
    if (revoked_ || busy_) return false;
    if (!known_) return true;
    Busy busy(busy_);
    return Publish(runtime, &player) && (!nightKnown_ || PublishNight(runtime, &player));
}
bool CCSOLChallengeEnvironment::PublishNight(const Runtime& runtime, CBasePlayer* player)
{
    namespace custom = csol::scenario::custom;
    if (revoked_ || !nightKnown_ || runtime_.lock() != runtime || night_.revision == UINT32_MAX) return false;
    const auto generation = runtime->Generation();
    if (!Current(runtime, generation)) return false;
    const auto session = runtime->session;
    const auto round = session->Map().RoundGeneration();
    const EHANDLE recipient(player);
    if (player && (player->has_disconnected || !player->pev ||
        (player->pev->flags & (FL_FAKECLIENT | FL_KILLME | FL_DORMANT)))) return true;
    ++night_.revision;
    const auto bytes = custom::EncodeNightLighting(night_);
    if (!bytes) return false;
    if (player) MESSAGE_BEGIN(MSG_ONE, nightMessage_, nullptr, player->pev);
    else MESSAGE_BEGIN(MSG_ALL, nightMessage_);
    for (const auto byte : *bytes) WRITE_BYTE(std::to_integer<unsigned>(byte));
    MESSAGE_END();
    return !revoked_ && Current(runtime, generation) && runtime->session == session &&
        session->Map().RoundGeneration() == round && (!player || recipient.Get<CBasePlayer>() == player);
}
bool CCSOLChallengeEnvironment::NightFlashlightsAvailable() const noexcept
{
    return flashlightView_ && flashlightPlayer_ && !revoked_ && nightKnown_ &&
        night_.phase == csol::scenario::custom::Phase::Night;
}

bool CCSOLChallengeEnvironment::HoldsNightFlashlight(CBasePlayer& player) const
{
    if (!NightFlashlightsAvailable() || player.m_pActiveItem || player.pev->viewmodel != flashlightView_)
        return false;
    const int slot = player.entindex() - 1;
    return slot >= 0 && slot < int(nightFlashlights_.size()) &&
        nightFlashlights_[slot].Get<CBasePlayer>() == &player;
}

void CCSOLChallengeEnvironment::SelectNightFlashlight(CBasePlayer& player)
{
    if (!NightFlashlightsAvailable() || busy_ || !player.IsAlive() || player.m_pTank ||
        player.m_bIsDefusing || !(player.pev->weapons & (1 << WEAPON_SUIT))) return;

    const int slot = player.entindex() - 1;
    if (slot < 0 || slot >= int(nightFlashlights_.size())) return;
    auto& owner = nightFlashlights_[slot];
    if (HoldsNightFlashlight(player)) return;

    const auto runtime = runtime_.lock();
    const EHANDLE handle(&player);
    const EHANDLE weapon(player.m_pActiveItem);
    const int life = player.m_iNumSpawns;
    const auto current = [&] {
        return runtime && Current(runtime, runtime->Generation()) && NightFlashlightsAvailable() &&
            handle.Get<CBasePlayer>() == &player && !player.has_disconnected &&
            player.m_iNumSpawns == life && player.m_iTeam == CT && player.IsAlive();
    };
    if (!current()) return;

    Busy busy(busy_);
    auto* previous = weapon.Get<CBasePlayerItem>();
    if (previous)
    {
        if (!previous->CanHolster() || !current() || weapon.Get<CBasePlayerItem>() != previous ||
            player.m_pActiveItem != previous) return;
        player.ResetAutoaim();
        if (!current() || weapon.Get<CBasePlayerItem>() != previous || player.m_pActiveItem != previous) return;
        previous->Holster();
        if (!current() || weapon.Get<CBasePlayerItem>() != previous || player.m_pActiveItem != previous) return;
        player.m_pLastItem = previous;
    }

    owner = &player;
    player.m_pActiveItem = nullptr;
    player.m_bShieldDrawn = false;
    player.m_bResumeZoom = false;
    player.pev->fov = player.m_iFOV = player.m_iLastZoom = DEFAULT_FOV;
    player.pev->viewmodel = flashlightView_;
    player.pev->weaponmodel = flashlightPlayer_;
    player.pev->weaponanim = 0;
    Q_strncpy(player.m_szAnimExtention, "knife", sizeof(player.m_szAnimExtention));
    player.ResetMaxSpeed();
    if (current() && HoldsNightFlashlight(player) && !player.FlashlightIsOn() && player.m_iFlashBattery > 0)
        player.FlashlightTurnOn();
}

void CCSOLChallengeEnvironment::CheckNightFlashlight(CBasePlayer& player)
{
    if (!NightFlashlightsAvailable() || (!player.m_pActiveItem && player.pev->viewmodel == flashlightView_))
        return;
    // The active weapon replaces the hand models. The light remains on the player
    // until it is switched off, the battery runs out, or the night ends.
    ClearFlashlightModels(player, flashlightView_, flashlightPlayer_);
}

void CCSOLChallengeEnvironment::ReleaseNightFlashlight(CBasePlayer& player, bool holster)
{
    const int slot = player.entindex() - 1;
    if (slot < 0 || slot >= int(nightFlashlights_.size())) return;
    auto& owner = nightFlashlights_[slot];
    if (owner.Get<CBasePlayer>() != &player) return;
    const EHANDLE playerHandle = owner;
    owner = EHANDLE{};
    const bool liveHolster = holster && !revoked_ && !player.has_disconnected && player.IsAlive();
    const int life = player.m_iNumSpawns;
    ClearFlashlightModels(player, flashlightView_, flashlightPlayer_);
    if (player.FlashlightIsOn())
    {
        if (liveHolster) player.FlashlightTurnOff();
        else
        {
            player.pev->effects &= ~EF_DIMLIGHT;
            player.m_flFlashLightTime = gpGlobals->time + FLASH_CHARGE_TIME;
        }
    }
    if (liveHolster && !revoked_ && !owner.Get<CBasePlayer>() && playerHandle.Get<CBasePlayer>() == &player &&
        player.m_iNumSpawns == life && !player.m_pActiveItem && !player.has_disconnected && player.IsAlive())
        player.SelectLastItem();
}

void CCSOLChallengeEnvironment::ClearNightFlashlights(bool holster)
{
    for (auto& owner : nightFlashlights_)
    {
        if (auto* player = owner.Get<CBasePlayer>()) ReleaseNightFlashlight(*player, holster);
        else owner = EHANDLE{};
    }
}

void CCSOLChallengeEnvironment::Revoke() noexcept
{
    if (revoked_) return;
    revoked_ = true;
    ClearNightFlashlights(false);
}

void CCSOLChallengeEnvironment::Restore()
{
    Revoke();
    if (busy_ || !lightOwner_) return;
    const auto owner = lightOwner_; lightOwner_ = 0;
    // The engine checks the actual spawncount and retires stale tokens. A map
    // already gone must never receive an old world's restoration pattern.
    if (lights_) lights_->Release(owner);
}
