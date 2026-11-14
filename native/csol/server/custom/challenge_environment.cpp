// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "challenge_environment.h"
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
    if (!config) return false;
    const auto factory = Sys_GetFactory("swds.dll");
    lights_ = factory ? static_cast<goldcraft::IMapLightStyleService*>(
        factory(goldcraft::MapLightStyleServiceVersion, nullptr)) : nullptr;
    if (!lights_) return false;
    message_ = REG_USER_MSG(custom::EnvironmentMessage, static_cast<int>(custom::EnvironmentMessageSize));
    if (revoked_ || message_ < 64 || message_ > 255) return false;
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
        challenge_ = challenge; runtime_ = runtime;
        snapshot_ = {}; snapshot_.owner = ++environmentSerial; snapshot_.profile = config_.fog;
        known_ = false;
    }
    else if (runtime_.lock() != runtime) return false;
    const auto value = fade_.Advance(challenge, phase, now);
    if (!value) return false;
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
    Busy busy(busy_); return Publish(runtime, &player);
}
void CCSOLChallengeEnvironment::Restore()
{
    revoked_ = true;
    if (busy_ || !lightOwner_) return;
    const auto owner = lightOwner_; lightOwner_ = 0;
    // The engine checks the actual spawncount and retires stale tokens. A map
    // already gone must never receive an old world's restoration pattern.
    if (lights_) lights_->Release(owner);
}
