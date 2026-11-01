// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_oberon_knife_effect.h"
#include "scenario_monster_factory.h"
#include "scenario_session.h"
#include "game_rules_gate.h"

#include <cmath>

#pragma clang fp contract(off)

LINK_ENTITY_TO_CLASS(knifeeffect, CCSOLOberonKnifeEffect, CCSEntity)

struct CSOLOberonKnifeEffectState
{
    std::weak_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<const CSOLScenarioEntityAccess> access;
    EHANDLE entity;
    const CCSOLOberonKnifeEffect* identity{};
    std::uint64_t revision{}, playRound{};
    float deadline{};
    bool spawned{}, spawning{}, playing{}, busy{}, retired{};

    CCSOLOberonKnifeEffect* Attached() const
    {
        if (retired || !access || !access->Allowed()) return nullptr;
        auto* current = dynamic_cast<CCSOLOberonKnifeEffect*>(entity.Get<CBaseEntity>());
        return current && current == identity && current->m_state.get() == this &&
            !(current->pev->flags & FL_KILLME) ? current : nullptr;
    }
    CCSOLOberonKnifeEffect* Current() const
    {
        const auto owner = session.lock();
        return owner && owner->State() == CSOLScenarioSessionState::Active &&
            owner->Map().EntityAccess() == access ? Attached() : nullptr;
    }
    void Hide()
    {
        ++revision;
        playing = false;
        if (auto* current = Attached())
        {
            current->pev->effects |= EF_NODRAW;
            current->SetThink(nullptr);
            current->pev->nextthink = 0;
        }
    }
    void Remove()
    {
        auto* current = Attached();
        ++revision;
        playing = false;
        retired = true;
        if (current)
        {
            current->SetThink(nullptr);
            current->pev->nextthink = 0;
            UTIL_Remove(current);
        }
    }
};

namespace
{
bool Finite(const Vector& value)
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
struct Operation
{
    std::shared_ptr<CSOLOberonKnifeEffectState> state;
    explicit Operation(std::shared_ptr<CSOLOberonKnifeEffectState> value) : state(std::move(value))
    { state->busy = true; }
    ~Operation() { state->busy = false; }
};
}

CCSOLOberonKnifeEffects::~CCSOLOberonKnifeEffects() { Shutdown(); }

bool CCSOLOberonKnifeEffects::Initialize(const std::shared_ptr<CCSOLScenarioSession>& session,
    csol::scenario::ConfigError& error)
{
    const auto keepAlive = weak_from_this().lock();
    if (!keepAlive || !session || m_state) return false;
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    const auto access = session->Map().EntityAccess();
    const auto validSession = [&]
    {
        return access && access->Allowed() && sdk.Current() &&
            session->State() == CSOLScenarioSessionState::Active &&
            session->Map().EntityAccess() == access;
    };
    if (!validSession()) return false;
    if (!gpGlobals || !std::isfinite(gpGlobals->time) || !g_engfuncs.pfnModelIndex)
    { error.message = "Oberon knife effect needs an engine clock and model lookup"; return false; }
    auto state = std::make_shared<CSOLOberonKnifeEffectState>();
    state->session = session;
    state->access = access;
    m_state = state;
    Operation operation(state);
    const auto revision = state->revision;
    const auto valid = [&]
    { return m_state == state && state->revision == revision && !state->retired && validSession(); };
    // Publish the operation before the first engine callback so a recursive
    // Initialize cannot allocate another manager entity. ModelIndex only
    // looks up the model already precached during worldspawn.
    const int model = MODEL_INDEX(CSOLOberonKnifeEffectModel);
    if (!valid()) { state->Remove(); return false; }
    if (model <= 0)
    {
        state->Remove();
        error.message = "Oberon knife effect model was not precached";
        return false;
    }
    const auto name = ALLOC_STRING("knifeeffect");
    if (!valid()) { state->Remove(); return false; }
    const auto edict = CREATE_NAMED_ENTITY(name);
    if (!access->Allowed()) { state->Remove(); return false; }
    EHANDLE created(edict);
    const auto cancelAllocation = [&]
    {
        if (!access->Allowed()) return;
        auto* pending = dynamic_cast<CCSOLOberonKnifeEffect*>(created.Get<CBaseEntity>());
        if (pending && !pending->m_state && !(pending->pev->flags & FL_KILLME)) UTIL_Remove(pending);
    };
    if (!valid()) { cancelAllocation(); state->Remove(); return false; }
    const bool absent = FNullEnt(edict);
    if (!valid()) { cancelAllocation(); state->Remove(); return false; }
    auto* entity = absent ? nullptr : dynamic_cast<CCSOLOberonKnifeEffect*>(created.Get<CBaseEntity>());
    if (!entity || entity->m_state || (entity->pev->flags & FL_KILLME))
    {
        state->Remove();
        error.message = "Oberon reusable knife effect allocation failed";
        return false;
    }
    state->entity = entity;
    state->identity = entity;
    entity->m_state = state;
    entity->pev->angles = g_vecZero;
    UTIL_SetOrigin(entity->pev, g_vecZero);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    const int result = DispatchSpawn(edict);
    if (!valid() || !state->Current()) { state->Remove(); return false; }
    if (result < 0 || !state->spawned)
    {
        state->Remove();
        error.message = "Oberon reusable knife effect Spawn failed";
        return false;
    }
    error = {};
    return true;
}

void CCSOLOberonKnifeEffect::Spawn()
{
    const auto state = m_state;
    if (!state || state->spawned || state->spawning || !state->Current()) return;
    const auto session = state->session.lock();
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    if (!sdk.Current()) return;
    struct SpawnScope { bool& flag; ~SpawnScope() { flag = false; } } scope{state->spawning};
    state->spawning = true;
    pev->movetype = MOVETYPE_FLY;
    pev->solid = SOLID_NOT;
    pev->effects |= EF_NODRAW;
    SET_MODEL(edict(), CSOLOberonKnifeEffectModel);
    auto* current = sdk.Current() ? state->Current() : nullptr;
    if (!current) return;
    current->pev->rendermode = kRenderTransAdd;
    current->pev->renderfx = kRenderFxNone;
    current->pev->renderamt = 255.0f;
    current->SetThink(nullptr);
    current->pev->nextthink = 0;
    state->spawned = true;
}

bool CCSOLOberonKnifeEffects::Play(CSOLMonsterSpawnCall& creator, int skill,
    csol::scenario::ConfigError& error)
{
    const auto keepAlive = weak_from_this().lock();
    const auto state = m_state;
    if (!keepAlive || !state || state->busy || !state->spawned || !state->Current()) return false;
    const auto session = state->session.lock();
    if (!session || !creator.BelongsTo(session->Map()) || !creator.Current()) return false;
    if (skill != 3 && skill != 4 && skill != 6)
    { error.message = "Oberon knife effect has no sequence for this skill"; return false; }
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    if (!sdk.Current() || !gpGlobals) return false;
    Operation operation(state);
    const auto revision = ++state->revision;
    const auto round = session->Map().RoundGeneration();
    const auto valid = [&]() -> CCSOLOberonKnifeEffect*
    {
        return m_state == state && state->revision == revision && sdk.Current() &&
            session->Map().RoundGeneration() == round && creator.Current() ? state->Current() : nullptr;
    };
    // MP113D7274 samples nextthink's base before ResetSequenceInfo, but the
    // fade deadline samples the clock afterwards. Keep these reads separate.
    const float startedAt = gpGlobals->time;
    const Vector origin = creator.Current()->pev->origin;
    const Vector angles = creator.Current()->pev->angles;
    if (!std::isfinite(startedAt + 0.05f) || !Finite(origin) || !Finite(angles))
    { error.message = "Oberon knife effect position or clock is not finite"; return false; }
    auto* entity = valid();
    if (!entity) return false;
    state->playing = false;
    entity->SetThink(nullptr);
    entity->pev->nextthink = 0;
    entity->pev->angles = angles;
    // Relink the GoldSrc entity at the sampled location. It never follows
    // the monster or a bone after this one placement.
    UTIL_SetOrigin(entity->pev, origin);
    entity = valid();
    if (!entity)
    {
        if (m_state == state && state->revision == revision) state->Hide();
        return false;
    }
    entity->pev->effects &= ~EF_NODRAW;
    entity->pev->sequence = skill == 3 ? 0 : skill == 4 ? 1 : 2;
    const bool reset = CSOL_ResetScenarioSequenceInfo(*entity, [](const void* context) -> CBaseAnimating*
    { return (*static_cast<const decltype(valid)*>(context))(); }, &valid);
    entity = valid();
    if (!entity)
    {
        if (m_state == state && state->revision == revision) state->Hide();
        return false;
    }
    const float deadline = gpGlobals->time + 0.75f;
    if (!reset || !std::isfinite(deadline))
    {
        state->Hide();
        error.message = "Oberon knife effect animation or fade clock failed";
        return false;
    }
    entity->pev->renderamt = 255.0f;
    state->deadline = deadline;
    state->playRound = round;
    state->playing = true;
    entity->pev->nextthink = startedAt + 0.05f;
    entity->SetThink(&CCSOLOberonKnifeEffect::FadeThink);
    error = {};
    return true;
}

void CCSOLOberonKnifeEffect::FadeThink()
{
    const auto state = m_state;
    if (!state || state->busy || state->retired || !state->playing) return;
    const auto session = state->session.lock();
    CSOLScenarioSdkScope sdk(CSOL_ScenarioOwnerRules(session.get()));
    auto* current = sdk.Current() ? state->Current() : nullptr;
    if (!current || session->Map().RoundGeneration() != state->playRound)
    { state->Hide(); return; }
    if (!gpGlobals || !std::isfinite(gpGlobals->time)) { state->Hide(); return; }
    current->pev->renderamt -= 17.0f;
    current->pev->nextthink = gpGlobals->time + 0.05f;
    if (gpGlobals->time >= state->deadline || current->pev->renderamt <= 0)
    {
        current->pev->effects |= EF_NODRAW;
        current->SetThink(nullptr);
        state->playing = false;
    }
    // Original FadeOut has no StudioFrameAdvance and does not remove the
    // entity. The next attack reuses it and resets its sequence clock.
}

void CCSOLOberonKnifeEffects::Hide()
{
    const auto state = m_state;
    if (state) state->Hide();
}

void CCSOLOberonKnifeEffects::Shutdown()
{
    auto state = std::move(m_state);
    if (state) state->Remove();
}

void CCSOLOberonKnifeEffect::OnDestroy()
{
    if (m_state)
    {
        ++m_state->revision;
        m_state->retired = true;
        m_state->playing = false;
    }
    m_state.reset();
    CBaseAnimating::OnDestroy();
}
