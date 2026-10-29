// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_player_runtime.h"
#include "scenario_death_owner.h"
#include "scenario_monster_damage.h"
#include "scenario_runtime.h"
#include "characters.h"
#include "scenario_entity_lookup.h"
#include "weapons.h"
#include "../scenario/monster_appearances.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <typeinfo>

namespace
{
bool HasStatistics(const CSOLScenarioPlayerRuntimeState& state) noexcept
{
    return state.generation && !state.statisticsRetired && !(state.weaponKills.empty() && state.weaponDamage.empty() &&
        !state.monsterHeadHits && !state.monsterDamageHits && !state.monsterKillChain &&
        !state.bestMonsterKillChain && !state.capEventPending && !state.capEventSequence &&
        !state.classification.a && !state.classification.b && !state.classification.c &&
        !state.classification.d && !state.classification.mixed && !state.bossDamage &&
        !state.ordinaryMonsterDamage && !state.lastMonsterKillAt);
}

std::optional<CSOLScenarioPlayerStatistics> CaptureStatistics(const CSOLScenarioPlayerRuntimeState& state)
{
    if (!HasStatistics(state)) return {};
    return CSOLScenarioPlayerStatistics{state.generation, state.capEventSequence, state.classId,
        state.capEventPending, state.weaponKills, state.weaponDamage, state.monsterHeadHits,
        state.monsterDamageHits, state.monsterKillChain, state.bestMonsterKillChain,
        state.bossDamage, state.ordinaryMonsterDamage, state.lastMonsterKillAt, state.classification};
}

// MP 11BEF298 starts at one, returns the old word and increments globally.
// Unsigned arithmetic retains its defined 32-bit rollover across maps.
std::uint32_t nextStatusLabel = 1;

// Addon pre-damage uses x87 intermediates, with a single float32 store after
// each expression. Preserve the thread's control word, as the original does.
float AddonProductAdd(float value, float multiplier, float addend)
{
    float result;
#if defined(_MSC_VER) && defined(_M_IX86)
    __asm
    {
        fld value
        fmul multiplier
        fadd addend
        fstp result
    }
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ volatile("flds %1; fmuls %2; fadds %3; fstps %0"
        : "=m"(result) : "m"(value), "m"(multiplier), "m"(addend) : "st");
#else
#error Scenario addon damage requires the supported x86 x87 target
#endif
    return result;
}

float AddonAdd(float value, float addend)
{
    float result;
#if defined(_MSC_VER) && defined(_M_IX86)
    __asm
    {
        fld value
        fadd addend
        fstp result
    }
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ volatile("flds %1; fadds %2; fstps %0"
        : "=m"(result) : "m"(value), "m"(addend) : "st");
#else
#error Scenario addon damage requires the supported x86 x87 target
#endif
    return result;
}

float AddonProduct(float value, float multiplier)
{
    float result;
#if defined(_MSC_VER) && defined(_M_IX86)
    __asm
    {
        fld value
        fmul multiplier
        fstp result
    }
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ volatile("flds %1; fmuls %2; fstps %0"
        : "=m"(result) : "m"(value), "m"(multiplier) : "st");
#else
#error Scenario addon damage requires the supported x86 x87 target
#endif
    return result;
}

void UpdateProtectionRender(CSOLScenarioPlayerRuntimeState& state, CBasePlayer& player, bool removed)
{
    const bool held = std::any_of(state.buffs.begin(), state.buffs.end(),
        [](const auto& buff) { return buff.renderHeld; });
    if (held)
    {
        player.pev->rendermode = kRenderNormal;
        player.pev->renderfx = kRenderFxGlowShell;
        player.pev->rendercolor = Vector(201, 201, 201);
    }
    else if (removed && player.pev->renderfx == kRenderFxGlowShell && player.pev->rendercolor.z == 201)
    {
        // MP 107D2576 tests the effect and blue component, not a saved color
        // snapshot. Do not overwrite a newer unrelated render effect.
        player.pev->rendermode = kRenderNormal;
        player.pev->renderfx = kRenderFxNone;
        player.pev->rendercolor = g_vecZero;
    }
}

std::vector<CSOLScenarioStatBuff> ReleaseProtection(CSOLScenarioPlayerRuntimeState& state, CBasePlayer* player)
{
    bool held{};
    for (auto& buff : state.buffs)
    {
        held |= buff.renderHeld;
        buff.renderHeld = false;
    }
    if (player && held) UpdateProtectionRender(state, *player, true);
    auto retired = std::move(state.buffs);
    state.buffs = {};
    state.labels.clear();
    state.publishedLabels.clear();
    state.labelsDirty = false;
    ++state.buffsRevision;
    return retired; // Caller releases conditions only after retiring owned state.
}
}

bool CCSOLScenarioPlayerRuntime::Active() const
{
    return m_active && !m_invalidating && !m_releasingBuffs && m_error.message.empty() && m_session && m_access && m_access->Allowed() &&
        m_session->Map().EntityAccess() == m_access &&
        m_session->State() == CSOLScenarioSessionState::Active;
}

void CCSOLScenarioPlayerRuntime::RetainStatistics(CSOLScenarioPlayerRuntimeState& state) noexcept
{
    if (!state.generation || state.statisticsRetired) return;
    if (!HasStatistics(state)) { state.statisticsRetired = true; return; }
    // Every admitted state owns its preallocated node. Never allocate in
    // ClientDisconnect or after detaching the registry during Invalidate.
    auto& value = state.statisticsRetirement.front();
    value.generation = state.generation;
    value.capEventSequence = state.capEventSequence;
    value.classId = state.classId;
    value.capEventPending = state.capEventPending;
    // MSVC map move construction allocates its sentinel. Swap into the maps
    // which were already constructed in the admission node instead.
    value.weaponKills.swap(state.weaponKills);
    value.weaponDamage.swap(state.weaponDamage);
    value.headHits = state.monsterHeadHits;
    value.damageHits = state.monsterDamageHits;
    value.killChain = state.monsterKillChain;
    value.bestKillChain = state.bestMonsterKillChain;
    value.bossDamage = state.bossDamage;
    value.ordinaryDamage = state.ordinaryMonsterDamage;
    value.lastKillAt = state.lastMonsterKillAt;
    value.classification = state.classification;
    state.statisticsRetired = true;
    m_retiredStatistics.splice(m_retiredStatistics.end(), state.statisticsRetirement);
}

std::vector<CSOLScenarioPlayerStatistics> CCSOLScenarioPlayerRuntime::StatisticsSnapshot() const
{
    std::vector<CSOLScenarioPlayerStatistics> result(m_retiredStatistics.begin(), m_retiredStatistics.end());
    for (const auto& state : m_players)
        if (state)
            if (auto value = CaptureStatistics(*state)) result.push_back(std::move(*value));
    return result;
}

CSOLPlayerRecoveryCall::CSOLPlayerRecoveryCall(std::shared_ptr<CCSOLScenarioRuntime> runtime,
    EHANDLE player, bool (*ownerValid)(const void*), const void* ownerContext)
    : m_runtime(std::move(runtime)), m_player(player), m_ownerValid(ownerValid), m_ownerContext(ownerContext)
{
    if ((m_ownerValid && !m_ownerValid(m_ownerContext)) || !m_runtime || m_runtime->Closing()) return;
    m_session = m_runtime->session;
    m_players = m_runtime->players;
    if (!m_session || !m_players || !m_players->Active()) return;
    m_runtimeGeneration = m_runtime->Generation();
    m_roundGeneration = m_session->Map().RoundGeneration();
    m_state = m_players->Find(m_player);
}

CBasePlayer* CSOLPlayerRecoveryCall::Current() const
{
    if (m_cancelled || (m_ownerValid && !m_ownerValid(m_ownerContext)) || !m_runtime ||
        m_runtime->Closing() || m_runtime->Generation() != m_runtimeGeneration ||
        !m_session || m_runtime->session != m_session || !m_players || m_runtime->players != m_players ||
        !m_players->Active() || m_session->Map().RoundGeneration() != m_roundGeneration ||
        !m_state || m_players->Find(m_player) != m_state) return nullptr;
    return m_player.Get<CBasePlayer>();
}

bool CCSOLScenarioPlayerRuntime::Fail(const char* reason)
{
    if (m_error.message.empty()) m_error.message = reason;
    return false;
}

bool CCSOLScenarioPlayerRuntime::Activate()
{
    auto keepAlive = shared_from_this();
    if (m_active || m_activating || m_invalidating || m_releasingBuffs || !m_session ||
        m_session->State() != CSOLScenarioSessionState::Active) return false;
    m_activating = true;
    struct ActivationScope { bool& flag; ~ActivationScope() { flag = false; } } scope{m_activating};
    const auto lifecycle = m_lifecycle;
    const auto weak = weak_from_this();
    if (weak.expired()) return Fail("scenario player authority must have a retained owner");
    // Equipment and upgrades keep only this weak callback, avoiding a cycle
    // with the session which owns both services.
    if (!m_session->BindEquipmentServices([weak](const EHANDLE& player)
        {
            const auto owner = weak.lock();
            return owner && owner->RecordCapEvent(player);
        })) return Fail("scenario equipment and upgrade services could not be installed");
    if (lifecycle != m_lifecycle || m_session->State() != CSOLScenarioSessionState::Active) return false;
    // Release any retired components while activation is still closed. A
    // destructor may invalidate us, but cannot recursively reopen this owner.
    auto retired = std::move(m_players);
    m_players = {};
    m_removedPlayers = {};
    auto retiredBuffs = std::move(m_retiredBuffs);
    m_retiredBuffs = {};
    auto retiredHitEvents = std::move(m_monsterHitEvents);
    retired = {};
    retiredBuffs.clear();
    retiredHitEvents.reset();
    if (lifecycle != m_lifecycle || m_session->State() != CSOLScenarioSessionState::Active) return false;
    m_killPositions.clear();
    m_killChainRecords.clear();
    m_retiredStatistics.clear();
    m_error = {};
    m_access = m_session->Map().EntityAccess();
    if (!m_access->Allowed()) return false;
    m_active = true;
    return true;
}

void CCSOLScenarioPlayerRuntime::Invalidate()
{
    auto keepAlive = shared_from_this();
    if (m_invalidating) return;
    m_invalidating = true;
    struct InvalidationScope { bool& flag; ~InvalidationScope() { flag = false; } } scope{m_invalidating};
    // Release render only while map access is valid; finalization after the
    // engine discards old edicts clears owned records without resolving them.
    ReleaseProtectionState();
    const auto weapons = m_session ? m_session->Weapons() : nullptr;
    const bool canRemove = m_access && m_access->Allowed() && weapons && weapons->Current();
    m_active = false;
    m_clearPointProtection = false;
    ++m_protectionOperation;
    ++m_lifecycle;
    // Keep value-only submissions available to the shutdown archive even
    // after an earlier explicit invalidation; no retired edict is resolved.
    // Detach the whole old registry before releasing any component. Its
    // destructor must not make this loop traverse a newly installed player.
    auto retired = std::move(m_players);
    m_players = {};
    m_removedPlayers = {};
    auto retiredBuffs = std::move(m_retiredBuffs);
    m_retiredBuffs = {};
    auto retiredHitEvents = std::move(m_monsterHitEvents);
    // Preserve every value before the first optional catalog callback. A
    // callback failure must not discard later detached player statistics.
    for (const auto& state : retired) if (state) RetainStatistics(*state);
    for (auto& state : retired)
    {
        if (state)
        {
            if (canRemove) weapons->RemovePlayer(state->player, state->generation);
            state->generation = 0;
        }
    }
    retired = {};
    retiredBuffs.clear();
    retiredHitEvents.reset();
}

std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Resolve(const EHANDLE& handle) const
{
    if (!Active()) return {};
    auto* player = dynamic_cast<CBasePlayer*>(handle.Get<CBaseEntity>());
    if (!player || player->has_disconnected || (player->pev->flags & FL_KILLME)) return {};
    for (const auto& state : m_players)
        if (state && state->generation && state->player.Get() == handle.Get()) return state;
    return {};
}

std::shared_ptr<const CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Find(const EHANDLE& player) const
{
    return Resolve(player);
}

bool CCSOLScenarioPlayerRuntime::WasRemoved(const EHANDLE& player) const
{
    if (!Active() || !player.Get()) return false;
    return std::any_of(m_removedPlayers.begin(), m_removedPlayers.end(),
        [&](const EHANDLE& retired) { return retired.Get() == player.Get(); });
}

std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Ensure(CBasePlayer& player)
{
    if (!Active()) return {};
    const auto lifecycle = m_lifecycle;
    const EHANDLE handle(&player);
    if (auto state = Resolve(handle)) return state;
    const int index = player.entindex();
    if (!Active() || m_lifecycle != lifecycle || handle.Get<CBaseEntity>() != &player || player.has_disconnected ||
        (player.pev->flags & FL_KILLME) || index < 1 || index >= static_cast<int>(m_players.size())) return {};
    // INDEXENT/ENTINDEX belongs to the engine. A nested server operation may
    // already have registered this player while the index was being read.
    if (auto state = Resolve(handle)) return state;
    auto& previous = m_players[index];
    auto retired = previous;
    std::vector<CSOLScenarioStatBuff> retiredBuffs;
    const auto weapons = m_session->Weapons();
    if (!weapons || !weapons->Current())
    { Fail("scenario player selection catalog is unavailable"); return {}; }
    // Complete allocations before touching the previous generation.
    auto state = std::make_shared<CSOLScenarioPlayerRuntimeState>();
    state->statisticsRetirement.emplace_back();
    state->displayAbilities = std::make_shared<CSOLScenarioDisplayAbilities>();
    if (previous)
    {
        RetainStatistics(*previous);
        retiredBuffs = ReleaseProtection(*previous, dynamic_cast<CBasePlayer*>(previous->player.Get<CBaseEntity>()));
        weapons->RemovePlayer(previous->player, previous->generation);
        previous->generation = 0;
    }
    state->player = handle;
    state->generation = ++m_generation;
    state->profileRevision = 1;
    // MP114121D0 clears the player registry before installing the mode15
    // defaults42/39/13/20/26/40. Their constructors/initializers do not add
    // keys3/28/29. Create that known initial directory once per player generation;
    // Prepare and profile changes must retain later dynamic registrations.
    if (!weapons->SelectItems(player, state->generation, weapons->DefaultSelection()))
    { Fail("scenario default knife or HE selection could not be installed"); return {}; }
    previous = state;
    m_removedPlayers[index] = static_cast<CBaseEntity*>(nullptr);
    retired.reset();
    retiredBuffs.clear();
    if (!Active() || m_lifecycle != lifecycle || Resolve(handle) != state) return {};
    return state;
}

bool CCSOLScenarioPlayerRuntime::Prepare(CBasePlayer& player)
{
    auto keepAlive = shared_from_this();
    const auto state = Ensure(player);
    if (!state || state->preparing) return false;
    struct Preparing { bool& value; ~Preparing() { value = false; } } preparing{state->preparing};
    state->preparing = true;
    int classId{};
    if (!CSOL_CharacterScenarioClass(&player, classId, m_access))
        return Fail("selected scenario character has no verified upgrade class");
    if (Resolve(state->player) != state) return false;
    const auto players = m_session->Players();
    const auto upgrades = players ? players->Upgrades(state->player) : nullptr;
    // This dedicated server owns new-player profiles instead of waiting for
    // CSO's account service. Seed the full verified preset catalog exactly
    // once, only for untouched local state. Explicit/modified profiles are
    // authoritative even when empty or incomplete; never overwrite them on
    // respawn, repeated preparation or character changes.
    if (m_session->ResourceScope() == CSOLScenarioResourceScope::Combat &&
        state->classModSource == CSOLScenarioClassModSource::Uninitialized)
    {
        const auto presets = m_session->ClassModPresets();
        if (!presets) return Fail("local scenario profile has no default ClassMod catalog");
        if (state->profileRevision == UINT64_MAX)
            return Fail("local scenario profile revision is exhausted");
        std::vector<csol::scenario::EquipmentLoadout::ClassCaps> records;
        records.reserve(presets->records.size());
        for (const auto& [id, record] : presets->records) records.push_back(record);
        state->profile.equipment.classCaps = std::move(records);
        state->profile.equipment.classModsComplete = true;
        state->classModSource = CSOLScenarioClassModSource::HostDefault;
        ++state->profileRevision;
    }
    // Selection needs no combat ClassMod directory. For combat, refresh only
    // complete records after class/profile/resource revision changes. An
    // explicitly incomplete import remains unknown, not an empty skill list.
    if (m_session->ResourceScope() == CSOLScenarioResourceScope::Combat &&
        state->profile.equipment.classModsComplete)
    {
        csol::scenario::ConfigError error;
        if (!RefreshClassMods(state->player, classId, error))
        {
            if (Resolve(state->player) != state) return false;
            return error.message.empty() ? false : Fail(error.message.c_str());
        }
    }
    if (state->classId == classId && state->appliedRevision == state->profileRevision &&
        upgrades && upgrades->ConfiguredClass() == classId) return true;
    const auto revision = state->profileRevision;
    // Bind the actual owned inventory before Configure publishes levels or a
    // weapon callback asks ResetMaxSpeed to consume them.
    if (!m_session->PrepareEquippedPlayer(player, classId, state->profile.characterFeature,
        state->profile.elevenLevelLimit, state->profile.equipment))
        return Fail("scenario player equipment or upgrade initialization failed");
    if (Resolve(state->player) != state || state->profileRevision != revision) return false;
    state->classId = classId;
    state->appliedRevision = revision;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetClassModRecords(CBasePlayer& player,
    std::vector<csol::scenario::EquipmentLoadout::ClassCaps> records)
{
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(EHANDLE(&player));
    if (!state || state->preparing) return false;
    auto profile = state->profile;
    profile.equipment.classCaps = std::move(records);
    profile.equipment.classModsComplete = true;
    return SetProfile(player, std::move(profile));
}

bool CCSOLScenarioPlayerRuntime::RefreshClassMods(const EHANDLE& player, int classId,
    csol::scenario::ConfigError& error)
{
    error = {};
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(player);
    if (!state) return false;
    if (!state->profile.equipment.classModsComplete)
    { error.message = "scenario class mod records have not been completely imported"; return false; }
    const auto table = m_session->ClassMods();
    if (!table) { error.message = "scenario class mod directory is unavailable"; return false; }
    if (state->classMods.building)
    { error.message = "scenario class mod rebuild is already active"; return false; }
    if (state->classMods.complete && state->classModProfileRevision == state->profileRevision &&
        state->classMods.classId == classId && state->classMods.table == table) return true;
    if (state->generation > UINT32_MAX || state->classModRevision == UINT64_MAX)
    { error.message = "scenario class mod identity is exhausted"; return false; }
    const auto profileRevision = state->profileRevision;
    const auto revision = ++state->classModRevision;
    // Stable copy: future callback-capable mode providers cannot leave a span
    // into a replaced profile. The validity gate stops the old build's suffix.
    const auto records = state->profile.equipment.classCaps;
    const auto current = [&]
    { return Resolve(player) == state && state->profileRevision == profileRevision &&
        state->classModRevision == revision && m_session->ClassMods() == table; };
    const csol::scenario::ClassModQuery query{
        [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current,
        [](const void*, int& mode) { mode = 15; return true; }, nullptr};
    const bool rebuilt = csol::scenario::RebuildClassMods(static_cast<std::uint32_t>(state->generation),
        classId, records, table, state->classMods, query, error);
    if (!current()) return false;
    if (!rebuilt || !error.message.empty()) return false;
    state->classModProfileRevision = profileRevision;
    return true;
}

bool CCSOLScenarioPlayerRuntime::CurrentClassMod(const EHANDLE& player, int type,
    std::optional<csol::scenario::ClassModDefinition>& result, csol::scenario::ConfigError& error)
{
    error = {};
    const auto keepAlive = shared_from_this();
    result.reset();
    const auto state = Resolve(player);
    if (!state) return false;
    if (!state->profile.equipment.classModsComplete || state->classModProfileRevision != state->profileRevision ||
        state->classMods.table != m_session->ClassMods())
    { error.message = "scenario class mod selection is unknown or stale"; return false; }
    const auto revision = state->classModRevision;
    const auto profileRevision = state->profileRevision;
    const auto table = state->classMods.table;
    const auto current = [&]
    { return Resolve(player) == state && state->classModRevision == revision &&
        state->profileRevision == profileRevision && state->classMods.table == table && m_session->ClassMods() == table; };
    const csol::scenario::ClassModQuery query{
        [](const void* value) { return (*static_cast<const decltype(current)*>(value))(); }, &current,
        [](const void*, int& mode) { mode = 15; return true; }, nullptr};
    const csol::scenario::ClassModDefinition* selected{};
    if (!csol::scenario::FindClassModType(state->classMods, type, query, selected, error) || !current()) return false;
    if (selected) result = *selected;
    return true;
}

bool CCSOLScenarioPlayerRuntime::CurrentClassModPair(const EHANDLE& player, int type, int subtype,
    std::optional<csol::scenario::ClassModDefinition>& result, csol::scenario::ConfigError& error)
{
    error = {};
    const auto keepAlive = shared_from_this();
    result.reset();
    const auto state = Resolve(player);
    if (!state) return false;
    if (!state->profile.equipment.classModsComplete || state->classModProfileRevision != state->profileRevision ||
        state->classMods.table != m_session->ClassMods())
    { error.message = "scenario class mod selection is unknown or stale"; return false; }
    const csol::scenario::ClassModDefinition* selected{};
    // v50 has no mode/engine callback. Copy the immutable definition while the
    // state and its directory snapshot are held by this synchronous operation.
    if (!csol::scenario::FindClassModPair(state->classMods, type, subtype, selected, error)) return false;
    if (selected) result = *selected;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetRecoveryBlockedUntil(const EHANDLE& player, double deadline)
{
    const auto state = Resolve(player);
    if (!state || !std::isfinite(deadline)) return false;
    state->recoveryBlockedUntil = deadline;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetSpecialRecoveryMaximum(const EHANDLE& player,
    std::optional<float> maximum)
{
    const auto state = Resolve(player);
    if (!state || (maximum && !std::isfinite(*maximum))) return false;
    state->specialRecoveryMaximum = maximum;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetRecoveryDisplayState(const EHANDLE& player,
    std::optional<std::uint8_t> displayState, std::optional<float> deadline)
{
    const auto state = Resolve(player);
    if (!state || (deadline && !std::isfinite(*deadline))) return false;
    state->recoveryDisplayState221B = displayState;
    state->recoveryDisplayDeadline2F38 = deadline;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetDisplayAbilities(const EHANDLE& player,
    std::shared_ptr<CSOLScenarioDisplayAbilities> abilities)
{
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(player);
    if (!state) return false;
    auto retired = std::move(state->displayAbilities);
    state->displayAbilities = abilities;
    retired.reset();
    return Resolve(player) == state && state->displayAbilities == abilities;
}

bool CCSOLScenarioPlayerRuntime::Feature3Active(const EHANDLE& player, bool& active,
    csol::scenario::ConfigError& error, bool (*ownerValid)(const void*), const void* ownerContext)
{
    const auto keepAlive = shared_from_this();
    if (ownerValid && !ownerValid(ownerContext)) return false;
    const auto state = Resolve(player);
    if (!state) return false;
    const auto abilities = state->displayAbilities;
    if (!abilities) { error.message = "scenario display ability directory is unavailable"; return false; }
    const auto feature = abilities->feature3;
    const auto current = [&]
    {
        return (!ownerValid || ownerValid(ownerContext)) && Resolve(player) == state &&
            state->displayAbilities == abilities && abilities->feature3 == feature;
    };
    if (!current()) return false;
    if (!feature) { active = false; return true; }
    const csol::scenario::Feature3Query query{
        [](const void* context) { return (*static_cast<const decltype(current)*>(context))(); }, &current,
        [](const void*, float& now)
        {
            if (!gpGlobals) return false;
            now = gpGlobals->time;
            return true;
        }, nullptr};
    bool value{};
    if (!feature->AnyActive(query, value, error) || !current()) return false;
    active = value;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SpecialRecoveryDisplayAllowed(const EHANDLE& player,
    bool& allowed, csol::scenario::ConfigError& error,
    bool (*ownerValid)(const void*), const void* ownerContext)
{
    const auto keepAlive = shared_from_this();
    if (ownerValid && !ownerValid(ownerContext)) return false;
    const auto state = Resolve(player);
    if (!state) return false;
    const auto abilities = state->displayAbilities;
    if (!abilities) { error.message = "scenario display ability directory is unavailable"; return false; }
    const auto current = [&]
    {
        return (!ownerValid || ownerValid(ownerContext)) && Resolve(player) == state &&
            state->displayAbilities == abilities;
    };
    if (!current()) return false;
    // Perform both key lookups before reading either value. These local
    // authoritative lookups are pure and do not dispatch ability callbacks.
    const auto* feature28 = abilities->feature28 ? &*abilities->feature28 : nullptr;
    const auto* feature29 = abilities->feature29 ? &*abilities->feature29 : nullptr;
    const bool specialCategory = (feature28 && *feature28 != 0) || (feature29 && *feature29 > 0);
    if (!specialCategory) { allowed = true; return true; }
    bool active{};
    if (!Feature3Active(player, active, error,
        [](const void* context) { return (*static_cast<const decltype(current)*>(context))(); }, &current) ||
        !current()) return false;
    allowed = !active;
    return true;
}

bool CCSOLScenarioPlayerRuntime::ApplyAttributeRecovery(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, float amount, bool& applied,
    csol::scenario::ConfigError& error, bool (*ownerValid)(const void*), const void* ownerContext)
{
    applied = false;
    const auto keepAlive = shared_from_this();
    const auto valid = [&]
    { return (!ownerValid || ownerValid(ownerContext)) && call.Current(); };
    if (!valid() || !runtime || runtime->Closing() || runtime->session != m_session ||
        runtime->players.get() != this || !Active() || !m_session ||
        !call.Spawn().BelongsTo(m_session->Map())) return false;
    auto* player = call.Player();
    if (!player) { error.message = "attribute recovery has no player recipient"; return false; }
    if (!Resolve(EHANDLE(player)))
    { error.message = "attribute recovery player state is unavailable"; return false; }
    CSOLPlayerRecoveryCall recovery(std::move(runtime), EHANDLE(player),
        [](const void* context) { return (*static_cast<const decltype(valid)*>(context))(); }, &valid);
    try
    {
        const bool result = ApplyRecovery(recovery, amount, applied, error);
        if (recovery.Cancelled() || !recovery.Current()) return call.Cancel();
        return result;
    }
    catch (...)
    {
        if (recovery.Cancelled() || !recovery.Current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::ApplyRecovery(CSOLPlayerRecoveryCall& call,
    float amount, bool& applied, csol::scenario::ConfigError& error)
{
    applied = false;
    const auto keepAlive = shared_from_this();
    const auto runtime = call.Runtime();
    const auto session = m_session;
    const auto owned = [&]
    {
        return runtime && !runtime->Closing() && runtime->session == session &&
            runtime->players.get() == this && Active() && session &&
            call.Current();
    };
    if (!owned()) return false;
    auto* player = call.Player();
    if (!player) { error.message = "attribute recovery has no player recipient"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) { error.message = "attribute recovery player state is unavailable"; return false; }
    const auto current = [&]
    {
        return owned() && Resolve(handle) == state && call.Player() == player;
    };
    if (!current()) return call.Cancel();
    if (!std::isfinite(amount))
    { error.message = "attribute recovery amount is not finite"; return false; }
    const double blockedUntil = state->recoveryBlockedUntil;
    if (blockedUntil > 0)
    {
        if (!gpGlobals || !std::isfinite(gpGlobals->time))
        { error.message = "attribute recovery clock is unavailable"; return false; }
        // Equality permits recovery. The deadline retains double precision.
        if (blockedUntil > static_cast<double>(gpGlobals->time)) return true;
    }

    const float oldHealth = player->pev->health;
    if (player->pev->takedamage == DAMAGE_NO) return true;
    float maximum = player->pev->max_health;
    if (state->profile.weaponEffectsBlocked21F2)
    {
        if (!state->specialRecoveryMaximum)
        { error.message = "attribute recovery special health limit is unavailable"; return false; }
        maximum = *state->specialRecoveryMaximum;
    }
    if (!std::isfinite(oldHealth) || !std::isfinite(maximum))
    { error.message = "attribute recovery health state is not finite"; return false; }
    if (oldHealth >= maximum) return true;
    float health = oldHealth + amount;
    // ADDSS may overflow to +inf even for finite inputs; the original first
    // clamps that result to the finite upper limit, then computes the delta.
    if (health > maximum) health = maximum;
    if (!std::isfinite(health))
    { error.message = "attribute recovery health result is not finite"; return false; }
    // The ordinary branch clears bits&0x3FFF; kind7 passes bits=0, so the
    // player's damage bitset stays unchanged. Do not call SDK TakeHealth again.
    player->pev->health = health;
    applied = true;
    CSOLMonsterDeathContext context;
    const float displayAmount = player->pev->health - oldHealth;
    if (!std::isfinite(displayAmount))
    { error.message = "attribute recovery display delta is not finite"; return false; }
    if (displayAmount > 0)
    {
        const auto notice = runtime->recoveryNotice;
        if (!notice)
        { error.message = "attribute recovery notification policy is unavailable"; return false; }
        bool notified{};
        try
        {
            notified = notice->NotifySelfRecovery(call, displayAmount, context, error,
                [](const void* context) { return (*static_cast<const decltype(current)*>(context))(); }, &current);
        }
        catch (...)
        {
            if (!current() || runtime->recoveryNotice != notice) return call.Cancel();
            throw;
        }
        if (!current() || runtime->recoveryNotice != notice) return call.Cancel();
        if (!notified) return false; // A provider must explicitly cancel a normal interruption.
    }

    // Notification can change health or install/remove abilities. Sample the
    // new delta and then the current registry, once, as original v50 does.
    const float recovered = player->pev->health - oldHealth;
    if (!std::isfinite(recovered))
    { error.message = "attribute recovery callback delta is not finite"; return false; }
    if (m_componentDispatchDepth >= 32)
    { error.message = "scenario nested component notification limit exceeded"; return false; }
    ++m_componentDispatchDepth;
    struct DispatchScope { std::size_t& depth; ~DispatchScope() { --depth; } } scope{m_componentDispatchDepth};
    std::vector<std::uint64_t> tokens;
    tokens.reserve(state->killComponents.size());
    for (const auto& entry : state->killComponents) tokens.push_back(entry.token);
    for (const auto token : tokens)
    {
        bool success = true;
        try
        {
            std::shared_ptr<CSOLScenarioKillComponent> component;
            for (const auto& entry : state->killComponents)
                if (entry.token == token && entry.enabled) { component = entry.component; break; }
            if (component) success = component->HealthRecovered(call, recovered, context, error);
        }
        catch (...)
        {
            if (!current()) return call.Cancel();
            throw;
        }
        // Includes destruction after a component unregisters itself. The same
        // sampled delta reaches later components even if one changes health.
        if (!current()) return call.Cancel();
        if (!success) return false;
    }
    return true;
}

bool CCSOLScenarioPlayerRuntime::AddBloodSucking(const EHANDLE& player, float amount)
{
    const auto state = Resolve(player);
    if (!state || !state->bloodSuckingRegistered || !std::isfinite(amount)) return false;
    const float pending = state->bloodSuckingPending + amount;
    if (!std::isfinite(pending)) return false;
    state->bloodSuckingPending = pending;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetBloodSuckingCap(const EHANDLE& player, float percentage)
{
    const auto state = Resolve(player);
    if (!state || !state->bloodSuckingRegistered || !std::isfinite(percentage)) return false;
    state->bloodSuckingCapOverride = percentage; // Setter overwrites; max belongs to the consumer.
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetBloodSuckingEnabled(const EHANDLE& player, bool enabled)
{
    const auto state = Resolve(player);
    if (!state || !state->bloodSuckingRegistered) return false;
    state->bloodSuckingEnabled = enabled;
    return true;
}

bool CCSOLScenarioPlayerRuntime::UpdateBloodSucking(CSOLPlayerRecoveryCall& call,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto keepAlive = shared_from_this();
    const auto runtime = call.Runtime();
    if (!call.Current() || !runtime || runtime->players.get() != this) return call.Cancel();
    const auto state = Resolve(call.PlayerHandle());
    if (!state) { error.message = "scenario blood-sucking module owner is unavailable"; return false; }
    if (!state->bloodSuckingRegistered || !state->bloodSuckingEnabled ||
        state->bloodSuckingPending == 0) return true;
    // An engine PostThink is not recursively dispatched from a health hook.
    // Preserve additions made during recovery; the original clears them only
    // after returning from TakeHealth below.
    if (state->updatingBloodSucking) return true;
    struct Updating { bool& flag; ~Updating() { flag = false; } } updating{state->updatingBloodSucking};
    state->updatingBloodSucking = true;
    try
    {
        const bool alive = call.Player()->IsAlive() != FALSE;
        if (!call.Current()) return call.Cancel();
        if (!alive) { state->bloodSuckingPending = 0; return true; }
        const auto weapons = m_session->Weapons();
        if (!weapons) { error.message = "scenario blood-sucking weapon directory is unavailable"; return false; }
        double percentage{};
        const bool collected = weapons->BloodSuckingLimit(call, percentage, error);
        if (!call.Current() || m_session->Weapons() != weapons) return call.Cancel();
        if (!collected) return false;
        percentage = std::max(percentage, static_cast<double>(state->bloodSuckingCapOverride));
        const float maximum = call.Player()->pev->max_health;
        state->bloodSuckingCapOverride = 0;
        const double scaled = percentage * static_cast<double>(maximum);
        const double cap = scaled * 0.01;
        const float amount = static_cast<float>(std::min(static_cast<double>(state->bloodSuckingPending), cap));
        if (!std::isfinite(cap) || !std::isfinite(amount))
        { error.message = "scenario blood-sucking recovery cap is not finite"; return false; }
        state->bloodSuckingPending = amount;
        if (std::abs(amount) > 0x1p-52f)
        {
            bool applied{};
            const bool recovered = ApplyRecovery(call, amount, applied, error);
            if (!call.Current()) return call.Cancel();
            // A normal return consumes this update even if a later health
            // observer reports failure. Retrying would grant the same health
            // twice. Retirement/exception remains a separate host boundary.
            state->bloodSuckingPending = 0;
            return recovered;
        }
        state->bloodSuckingPending = 0;
        return true;
    }
    catch (...)
    {
        if (!call.Current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::SetProfile(CBasePlayer& player, CSOLScenarioPlayerProfile profile)
{
    auto keepAlive = shared_from_this();
    if (!Active()) return false;
    const auto weapons = m_session->Weapons();
    if (profile.selectedWeapons && (!weapons || !weapons->ValidSelection(*profile.selectedWeapons))) return false;
    const auto state = Ensure(player);
    if (!state || state->preparing) return false;
    if (profile.selectedWeapons)
    {
        if (!weapons || !weapons->SelectItems(player, state->generation, *profile.selectedWeapons)) return false;
    }
    state->profile = std::move(profile);
    state->classModSource = CSOLScenarioClassModSource::ExplicitProfile;
    ++state->profileRevision;
    return true;
}

void CCSOLScenarioPlayerRuntime::Remove(const EHANDLE& player)
{
    auto keepAlive = shared_from_this();
    if (!m_active) return;
    if (!m_access || !m_access->Allowed()) { Invalidate(); return; }
    // ClientDisconnect reaches this before has_disconnected is published.
    // Mark retirement before cleanup/destructors can reenter target selection.
    // EHANDLE serials keep this marker from excluding a new player in the slot.
    for (std::size_t index = 0; index < m_players.size(); ++index)
    {
        auto& state = m_players[index];
        if (!state || state->player.Get() != player.Get()) continue;
        RetainStatistics(*state);
        m_removedPlayers[index] = player;
        auto retired = std::move(state);
        auto retiredBuffs = ReleaseProtection(*retired, dynamic_cast<CBasePlayer*>(retired->player.Get<CBaseEntity>()));
        if (const auto weapons = m_session->Weapons()) weapons->RemovePlayer(retired->player, retired->generation);
        retired->generation = 0;
        // Finish removal of the old upgrade/equipment state before releasing
        // its components. There are no owner mutations after their destructors.
        m_session->RemovePlayer(player);
        return;
    }
    m_session->RemovePlayer(player);
}

void CCSOLScenarioPlayerRuntime::ReleaseProtectionState()
{
    const auto keepAlive = shared_from_this();
    if (m_releasingBuffs) return;
    m_releasingBuffs = true;
    struct Scope { bool& flag; ~Scope() { flag = false; } } scope{m_releasingBuffs};
    auto capacity = m_retiredBuffs.size();
    for (const auto& state : m_players) if (state) capacity += state->buffs.size();
    // Reserve before detaching any condition. Moving records below cannot
    // allocate and unexpectedly release a partial batch during unwinding.
    m_retiredBuffs.reserve(capacity);
    const bool accessible = m_access && m_access->Allowed();
    for (std::size_t index = 0; index < m_players.size(); ++index)
    {
        const auto& state = m_players[index];
        if (!state) continue;
        auto* player = accessible ? dynamic_cast<CBasePlayer*>(state->player.Get<CBaseEntity>()) : nullptr;
        auto retired = ReleaseProtection(*state, player);
        for (auto& buff : retired) m_retiredBuffs.push_back(std::move(buff));
    }
}

bool CCSOLScenarioPlayerRuntime::ProtectionBlocksDamage(const EHANDLE& player, float now) const
{
    const auto state = Resolve(player);
    if (!state) return false;
    bool any{}, allMask1 = true, allMask4 = true;
    for (const auto& buff : state->buffs)
    {
        if (!buff.protection || !buff.Active(now)) continue;
        any = true;
        allMask1 &= (buff.mask & 1) != 0;
        allMask4 &= (buff.mask & 4) != 0;
    }
    // The original root rejects protected damage only when neither complete
    // mask query succeeds. This differs from an any-matching-mask test.
    return any && !allMask1 && !allMask4;
}

bool CCSOLScenarioPlayerRuntime::RemoveProtectionAt(CSOLScenarioPlayerRuntimeState& state,
    CBasePlayer& player, std::size_t index)
{
    auto& buff = state.buffs[index];
    if (buff.renderHeld)
    {
        buff.renderHeld = false;
        UpdateProtectionRender(state, player, true);
    }
    // Render release precedes OnRemove's owned label deletions. A label of
    // the same type owned by another buff must remain independently live.
    for (const auto id : buff.labels)
    {
        std::erase_if(state.labels, [id](const auto& label) { return label.id == id; });
        state.labelsDirty = true;
    }
    auto condition = std::move(buff.condition);
    state.buffs.erase(state.buffs.begin() + static_cast<std::ptrdiff_t>(index));
    const auto revision = ++state.buffsRevision;
    condition.reset();
    return Resolve(state.player).get() == &state && state.buffsRevision == revision;
}

bool CCSOLScenarioPlayerRuntime::RemoveProtectionSource(CSOLScenarioPlayerRuntimeState& state,
    CBasePlayer& player, std::int32_t source, std::int32_t subsource)
{
    for (std::size_t index = 0; index < state.buffs.size();)
    {
        const auto& buff = state.buffs[index];
        if (buff.source == source && buff.subsource == subsource)
        { if (!RemoveProtectionAt(state, player, index)) return false; }
        else ++index;
    }
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetPlayerClearPointProtection(bool enabled)
{
    auto keepAlive = shared_from_this();
    if (!Active()) return false;
    if (enabled == m_clearPointProtection) return true;
    m_clearPointProtection = enabled;
    const auto lifecycle = m_lifecycle;
    const auto operation = ++m_protectionOperation;
    for (int index = 1; index <= gpGlobals->maxClients; ++index)
    {
        auto* entity = CSOL_ScenarioPlayerEntity(index, m_access);
        if (!Active() || lifecycle != m_lifecycle || operation != m_protectionOperation) return false;
        auto* player = dynamic_cast<CBasePlayer*>(entity);
        if (!player || player->m_iTeam != CT || player->has_disconnected ||
            static_cast<std::uint32_t>(player->pev->flags) == 0x80000000u ||
            (player->pev->flags & FL_KILLME)) continue;
        // No alive/deadflag filter: the original includes dead CT players.
        if (enabled)
        {
            if (!AddClearPointProtection(*player)) return false;
        }
        else if (auto state = Resolve(EHANDLE(player)))
        { if (!RemoveProtectionSource(*state, *player, 3, 0)) return false; }
        if (!Active() || lifecycle != m_lifecycle || operation != m_protectionOperation) return false;
    }
    return true;
}

bool CCSOLScenarioPlayerRuntime::AddClearPointProtection(CBasePlayer& player)
{
    const auto operation = m_protectionOperation;
    const auto state = Ensure(player);
    if (!state || operation != m_protectionOperation) return false;
    const float now = gpGlobals->time;
    if (!std::isfinite(now)) return Fail("scenario protection has an invalid clock");
    CSOLScenarioStatBuff buff;
    if (m_buffSequence == UINT64_MAX) return Fail("scenario buff identity exhausted");
    buff.token = ++m_buffSequence;
    buff.group = 2;
    buff.source = 3;
    buff.mask = 8;
    buff.protection = true;
    // The buff keeps constructor expiry=0. Its OnAdd creates a separate
    // type7 label with FLT_MAX duration, rather than SetDuration(0).
    const auto label = std::bit_cast<std::int32_t>(nextStatusLabel++);
    buff.labels.push_back(label);
    buff.renderHeld = true;
    const auto position = std::upper_bound(state->buffs.begin(), state->buffs.end(), buff.group,
        [](std::int32_t group, const auto& existing) { return group < existing.group; });
    state->buffs.insert(position, std::move(buff));
    state->labels.push_back({label, 7, 2, now + std::numeric_limits<float>::max(), now, false});
    state->labelsDirty = true;
    UpdateProtectionRender(*state, player, false);
    ++state->buffsRevision;
    return true;
}

bool CCSOLScenarioPlayerRuntime::PublishLabels(
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state, float now)
{
    if (!state->labelsDirty) return true;
    const auto current = [&]() { return Active() && Resolve(state->player) == state; };
    if (!current()) return false;
    state->labelsDirty = false;
    const auto revision = state->buffsRevision;
    std::vector<CSOLScenarioStatusLabel> desired;
    std::map<std::pair<std::int32_t, std::int32_t>, CSOLScenarioStatusLabel> grouped;
    std::map<std::int32_t, CSOLScenarioStatusLabel> ungrouped;
    for (const auto& label : state->labels)
    {
        if (label.group) grouped.try_emplace(std::pair{label.group, label.type}, label);
        else if (label.style) desired.push_back(label);
        else ungrouped.insert_or_assign(label.type, label);
    }
    for (const auto& [key, label] : grouped) desired.push_back(label);
    for (const auto& [type, label] : ungrouped) desired.push_back(label);
    const auto messages = m_session->Messages();
    const auto recipient = [&]() -> edict_t*
    {
        if (!current()) return nullptr;
        auto* player = state->player.Get<CBasePlayer>();
        if (!player || (player->pev->flags & FL_DORMANT) || !g_engfuncs.pfnEntOffsetOfPEntity) return nullptr;
        auto* entity = player->edict();
        const int offset = OFFSET(entity);
        if (!current() || state->buffsRevision != revision || !offset ||
            state->player.Get() != entity || (player->pev->flags & FL_DORMANT)) return nullptr;
        return entity;
    };
    // Snapshot values, never raw label pointers: a message hook may replace
    // the player's equipment. Keep the actual sent prefix for next-frame
    // reconciliation instead of continuing a stale desired list.
    const auto emitted = state->publishedLabels;
    for (const auto id : emitted)
    {
        if (std::any_of(desired.begin(), desired.end(),
            [id](const auto& label) { return label.id == id; })) continue;
        auto* entity = recipient();
        if (!entity) { state->labelsDirty = true; return current(); }
        const bool sent = messages.RemoveStatusLabel(entity, id);
        if (!current()) return false;
        if (!sent) { state->labelsDirty = true; return true; }
        std::erase(state->publishedLabels, id);
        if (state->buffsRevision != revision) { state->labelsDirty = true; return true; }
    }
    for (const auto& label : desired)
    {
        if (std::find(state->publishedLabels.begin(), state->publishedLabels.end(), label.id) !=
            state->publishedLabels.end()) continue;
        float remaining = label.deadline - now;
        if (label.deadline <= 0 || remaining > 86400.0f) remaining = -1.0f;
        auto* entity = recipient();
        if (!entity) { state->labelsDirty = true; return current(); }
        const bool sent = messages.SendStatusLabel(entity, label.id, label.style, label.type, remaining);
        if (!current()) return false;
        if (!sent) { state->labelsDirty = true; return true; }
        state->publishedLabels.push_back(label.id);
        if (state->buffsRevision != revision) { state->labelsDirty = true; return true; }
    }
    return true;
}

bool CCSOLScenarioPlayerRuntime::UpdatePlayerBuffs(CBasePlayer& player, float now)
{
    auto keepAlive = shared_from_this();
    if (!Active() || !std::isfinite(now)) return false;
    const auto state = Resolve(EHANDLE(&player));
    if (!state) return true;
    if (state->updatingBuffs) return true; // Retain dirty work for the next frame.
    struct Updating { bool& value; ~Updating() { value = false; } } updating{state->updatingBuffs};
    state->updatingBuffs = true;
    for (std::size_t index = 0; index < state->buffs.size();)
    {
        if (!state->buffs[index].Active(now))
        { if (!RemoveProtectionAt(*state, player, index)) return false; }
        else ++index;
    }
    // Label-only expiration uses strict >; a buff's own removal above uses
    // >= and deletes exactly the labels that belong to that buff.
    const auto removed = std::erase_if(state->labels,
        [now](const auto& label) { return label.deadline > 0 && now > label.deadline; });
    if (removed) { state->labelsDirty = true; ++state->buffsRevision; }
    return PublishLabels(state, now);
}

std::uint64_t CCSOLScenarioPlayerRuntime::InstallStatBuff(const EHANDLE& player, CSOLScenarioStatBuff buff)
{
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(player);
    if (!state || buff.token || buff.renderHeld || !buff.labels.empty() || m_buffSequence == UINT64_MAX ||
        !std::isfinite(buff.expires) || !std::isfinite(buff.criticalChance) ||
        !std::isfinite(buff.criticalMultiplier) || !std::isfinite(buff.attackRatio)) return 0;
    const auto token = buff.token = ++m_buffSequence;
    const auto position = std::upper_bound(state->buffs.begin(), state->buffs.end(), buff.group,
        [](std::int32_t group, const auto& existing) { return group < existing.group; });
    state->buffs.insert(position, std::move(buff));
    ++state->buffsRevision;
    return token;
}

bool CCSOLScenarioPlayerRuntime::RemoveStatBuff(const EHANDLE& handle, std::uint64_t token)
{
    const auto keepAlive = shared_from_this();
    const auto state = Resolve(handle);
    if (!state || !token) return false;
    auto* player = handle.Get<CBasePlayer>();
    if (!player) return false;
    const auto found = std::find_if(state->buffs.begin(), state->buffs.end(),
        [token](const auto& buff) { return buff.token == token; });
    return found != state->buffs.end() &&
        RemoveProtectionAt(*state, *player, static_cast<std::size_t>(found - state->buffs.begin()));
}

bool CCSOLScenarioPlayerRuntime::ApplyCriticalBuffDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this && runtime->session == m_session && Active(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return owned() && call.Current() && Resolve(handle) == state && call.Player() == player; };
    if (!gpGlobals || !std::isfinite(gpGlobals->time))
    { error.message = "scenario critical buff clock is invalid"; return false; }
    float chance{}, factorSum{};
    const auto add = [&](const CSOLScenarioStatBuff& buff)
    {
        chance += buff.criticalChance;
        factorSum += buff.criticalMultiplier;
    };
    for (std::size_t first = 0; first < state->buffs.size();)
    {
        const auto group = state->buffs[first].group;
        auto last = first + 1;
        while (last < state->buffs.size() && state->buffs[last].group == group) ++last;
        if (group == 1)
        {
            for (auto index = first; index < last; ++index)
            {
                const auto& buff = state->buffs[index];
                if (buff.Active(gpGlobals->time) && buff.criticalEnabled) add(buff);
            }
        }
        else
        {
            const CSOLScenarioStatBuff* best{};
            for (auto index = first; index < last; ++index)
            {
                const auto& buff = state->buffs[index];
                if (!buff.Active(gpGlobals->time)) continue;
                // The first candidate and final sum have no enabled gate.
                if (!best || !best->Active(gpGlobals->time)) { best = &buff; continue; }
                if (!buff.criticalEnabled) continue;
                if (!best->criticalEnabled || buff.criticalMultiplier > best->criticalMultiplier ||
                    (buff.criticalMultiplier == best->criticalMultiplier && buff.criticalChance > best->criticalChance))
                    best = &buff;
            }
            if (best && best->Active(gpGlobals->time)) add(*best);
        }
        first = last;
    }
    if (!std::isfinite(chance) || !std::isfinite(factorSum))
    { error.message = "scenario critical buff sum is not finite"; return false; }
    const float factor = factorSum > 1.0f ? factorSum : 1.0f;
    chance += 0.0f;
    if (!g_engfuncs.pfnRandomFloat)
    { error.message = "scenario critical buff random source is unavailable"; return false; }
    // Empty buffs still consume this exact engine RNG call. A callback may
    // change D or the buffs, but the already sampled factor/chance stay fixed.
    float roll{};
    try { roll = RANDOM_FLOAT(0.0f, 100.0f); }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
    if (!current()) return call.Cancel();
    if (!std::isfinite(roll))
    { error.message = "scenario critical buff random result is not finite"; return false; }
    if (chance >= roll)
    {
        const float damage = factor * call.damage;
        if (!std::isfinite(damage))
        { error.message = "scenario critical buff damage is not finite"; return false; }
        call.Current()->HostState().damageEffect = 8;
        call.damage = damage;
    }
    return true;
}

bool CCSOLScenarioPlayerRuntime::ApplyAttackBuffDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this && runtime->session == m_session && Active(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return owned() && call.Current() && Resolve(handle) == state && call.Player() == player; };
    auto* edict = player->pev ? player->pev->pContainingEntity : nullptr;
    if (!edict) return true;
    if (!g_engfuncs.pfnEntOffsetOfPEntity)
    { error.message = "scenario attack buff identity query is unavailable"; return false; }
    bool valid{};
    try { valid = OFFSET(edict) != 0; }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
    if (!current()) return call.Cancel();
    if (!valid || (player->pev->flags & FL_DORMANT)) return true;

    // No allocation/copy/sort in the damage query. Producers maintain group
    // order. Reentrant structural edits cancel traversal, a host adaptation
    // to protect indices; ordinary condition results are never memoized.
    const auto revision = state->buffsRevision;
    const auto stable = [&] { return current() && state->buffsRevision == revision; };
    const auto active = [&](std::size_t index, bool& result)
    {
        result = false;
        if (!stable()) return call.Cancel();
        if (!gpGlobals || !std::isfinite(gpGlobals->time))
        { error.message = "scenario attack buff clock is invalid"; return false; }
        result = index < state->buffs.size() && state->buffs[index].Active(gpGlobals->time);
        return true;
    };
    const auto eligible = [&](std::size_t index, bool& result)
    {
        if (!active(index, result)) return false;
        if (!result) return true;
        bool ok = true;
        try
        {
            auto condition = state->buffs[index].condition;
            if (condition) ok = condition->Evaluate(call, result, error);
        } // Include destruction of a self-unregistered condition in the check.
        catch (...)
        {
            if (!stable()) return call.Cancel();
            throw;
        }
        if (!stable()) return call.Cancel();
        return ok;
    };
    bool present{};
    for (std::size_t index = 0; index < state->buffs.size(); ++index)
    {
        if (!eligible(index, present)) return false;
        if (present) break;
    }
    if (!present) return true;
    float total{};
    for (std::size_t first = 0; first < state->buffs.size();)
    {
        const auto group = state->buffs[first].group;
        auto last = first + 1;
        while (last < state->buffs.size() && state->buffs[last].group == group) ++last;
        if (group == 0)
        {
            for (auto index = first; index < last; ++index)
            {
                bool applies{};
                if (!eligible(index, applies)) return false;
                if (applies) total += state->buffs[index].attackRatio;
            }
        }
        else
        {
            auto best = state->buffs.size();
            for (auto index = first; index < last; ++index)
            {
                bool applies{}, previous{};
                if (!eligible(index, applies) || !eligible(best, previous)) return false;
                if (!applies) { if (!previous) best = state->buffs.size(); }
                else if (!previous || state->buffs[index].attackRatio > state->buffs[best].attackRatio) best = index;
            }
            bool remains{};
            // Unlike selection, final commitment checks expiry alone.
            if (!active(best, remains)) return false;
            if (remains) total += state->buffs[best].attackRatio;
        }
        first = last;
    }
    if (!std::isfinite(total))
    { error.message = "scenario attack buff sum is not finite"; return false; }
    const float bonus = std::max(total, -1.0f);
    const float damage = static_cast<float>(static_cast<double>(call.damage) * (1.0 + static_cast<double>(bonus)));
    if (!std::isfinite(damage))
    { error.message = "scenario attack buff damage is not finite"; return false; }
    call.damage = damage;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetAddonDamageInputs(const EHANDLE& player,
    CSOLScenarioAddonDamageInputs inputs)
{
    const auto state = Resolve(player);
    if (!state || !std::isfinite(inputs.deadline1C)) return false;
    state->addonDamage = inputs;
    return true;
}

bool CCSOLScenarioPlayerRuntime::ApplyAddonDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this &&
        runtime->session == m_session && Active() && call.Current(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map())) return call.Cancel();
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) { error.message = "scenario addon player state is unavailable"; return false; }
    const auto tables = m_session->Equipment();
    if (!tables) { error.message = "scenario addon parameter directory is unavailable"; return false; }
    const auto current = [&]
    { return owned() && Resolve(handle) == state && call.Player() == player && m_session->Equipment() == tables; };
    const auto validEntity = [&](CBaseEntity& entity, bool& valid)
    {
        valid = false;
        if (!entity.pev || !entity.pev->pContainingEntity) return true;
        if (!g_engfuncs.pfnEntOffsetOfPEntity)
        { error.message = "scenario addon entity validation is unavailable"; return false; }
        const int offset = OFFSET(entity.pev->pContainingEntity);
        if (!current()) return call.Cancel();
        valid = offset && !(entity.pev->flags & FL_DORMANT);
        return true;
    };
    const auto level = [&](std::size_t index)
    {
        const auto& levels = state->profile.equipment.addonLevels;
        const auto found = levels.find(index);
        return found == levels.end() ? 0 : found->second;
    };
    const auto parameter = [&](std::size_t index, std::string_view key, std::string_view incrementKey)
    {
        if (level(index) <= 0 || index >= tables->addons.size()) return 0.0f;
        const auto& addon = tables->addons[index];
        const float base = addon.Parameter(key);
        const auto currentLevel = level(index);
        const auto increment = addon.parameters.find(incrementKey);
        if (currentLevel <= 1 || increment == addon.parameters.end()) return base;
        const float count = static_cast<float>(currentLevel - 1);
        const float increase = count * increment->second;
        return base + increase;
    };
    // D is a by-value input in107AA900. Callback edits to call.damage must
    // not replace that captured input, unlike the separate StatBuff consumer.
    float damage = call.damage;
    try
    {
        bool valid{};
        if (!validEntity(*player, valid)) return false;
        if (!valid || call.context.bytes[2]) { call.damage = damage; return true; }
        if (!validEntity(*call.Current(), valid)) return false;
        if (!valid) { call.damage = damage; return true; }
        float percent = 0.0f;
        if (level(1) > 0 && call.Inflictor() && call.Inflictor()->pev == player->pev)
        {
            auto* item = player->m_pActiveItem;
            if (item && !(item->pev->flags & FL_KILLME))
            {
                const EHANDLE before(item);
                const bool isWeapon = item->IsWeapon() != FALSE;
                if (!current()) return call.Cancel();
                if (isWeapon)
                {
                    if (player->m_pActiveItem == item && before.Get<CBaseEntity>() != item) return call.Cancel();
                    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
                    // No callbacks occur between this conversion and the two
                    // field reads. A zero module flag makes the conjunction
                    // false without inventing the unknown weapon+160 value.
                    if (weapon && !(weapon->pev->flags & FL_KILLME) && state->addonDamage.state11)
                    {
                        const auto weapons = m_session->Weapons();
                        if (!weapons || !weapons->Current())
                        { error.message = "scenario addon weapon state is unavailable"; return false; }
                        const auto value = weapons->AddonWeaponInput(*weapon);
                        if (!value)
                        { error.message = "scenario addon weapon input has not been supplied"; return false; }
                        if (*value >= 2) percent = AddonAdd(parameter(1,"DmgPercent","DmgPercentAdd"),0);
                    }
                }
            }
        }
        if (level(4) > 0 && state->addonDamage.count18 > 0)
        {
            const float count = static_cast<float>(state->addonDamage.count18);
            percent = AddonProductAdd(parameter(4,"DMGPercent","DMGPercentAdd"),count,percent);
        }
        if (level(7) > 0)
        {
            if (!gpGlobals) { error.message = "scenario addon clock is unavailable"; return false; }
            if (state->addonDamage.deadline1C > gpGlobals->time)
                percent = AddonAdd(parameter(7,"DmgPercent","DmgPercentAdd"),percent);
        }
        // Original skill-Boss predicate runs even without addon8. Repeated
        // Classify calls preserve the33/34 path and its callback boundaries.
        bool boss{};
        const int classification = call.Current()->Classify();
        if (!current()) return call.Cancel();
        if (classification == 17)
        {
            const auto* type = call.Current()->Type();
            boss = type && static_cast<std::int16_t>(type->skill) >= 1000;
        }
        else
        {
            const int second = call.Current()->Classify();
            if (!current()) return call.Cancel();
            if (second == 33) boss = true;
            else
            {
                const int third = call.Current()->Classify();
                if (!current()) return call.Cancel();
                boss = third == 34;
            }
        }
        if (boss && level(8) > 0)
            percent = AddonAdd(parameter(8,"DmgPercent","DmgPercentAdd"),percent);
        state->addonCriticalPending = false;
        if (level(2) > 0)
        {
            const float chance = parameter(2,"Percent","PercentAdd");
            const auto threshold = static_cast<double>(chance) >= -2147483648.0 &&
                static_cast<double>(chance) < 2147483648.0 ? static_cast<std::int32_t>(chance) : INT32_MIN;
            if (!g_engfuncs.pfnRandomLong)
            { error.message = "scenario addon probability generator is unavailable"; return false; }
            const int roll = RANDOM_LONG(0,99);
            if (!current()) return call.Cancel();
            if (roll < threshold)
            {
                const float increment = parameter(2,"DmgPercent","DmgPercentAdd");
                state->addonCriticalPending = true;
                percent = AddonAdd(increment,percent);
            }
        }
        if (level(25) > 0)
        {
            const float count = static_cast<float>(state->addonDamage.count48);
            const float product = AddonProduct(parameter(25,"DmgPercent","DmgPercentAdd"),count);
            const float maximum = parameter(25,"MaxPercent","MaxPercentAdd");
            // MINSS uses the second operand on equality/unordered.
            const float limited = maximum < product ? maximum : product;
            percent = limited + percent;
        }
        const float ratio = percent / 100.0f;
        const float scale = ratio + 1.0f;
        damage = scale * damage;
        if (level(0) > 0)
        {
            float factor = 1.0f;
            if (!validEntity(*player, valid)) return false;
            if (valid)
            {
                const auto players = m_session->Players();
                const auto upgrades = players ? players->Upgrades(handle) : nullptr;
                if (!upgrades || !upgrades->Current() ||
                    !upgrades->StatValue(csol::scenario::UpgradeStat::Damage,factor))
                { error.message = "scenario addon attack multiplier is unavailable"; return false; }
                if (!current() || m_session->Players() != players || players->Upgrades(handle) != upgrades)
                    return call.Cancel();
            }
            damage = AddonProductAdd(parameter(0,"Damage","DamageAdd"),factor,damage);
        }
        if (!std::isfinite(damage))
        { error.message = "scenario addon damage is not finite"; return false; }
        call.damage = damage;
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::FinishAddonDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this &&
        runtime->session == m_session && Active() && call.Current(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map())) return call.Cancel();
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) { error.message = "scenario addon post-damage player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && Resolve(handle) == state && call.Player() == player; };
    auto* target = call.Current();
    if (!target->pev || !target->pev->pContainingEntity) return true;
    if (!g_engfuncs.pfnEntOffsetOfPEntity)
    { error.message = "scenario addon post-damage target validation is unavailable"; return false; }
    try
    {
        const int offset = OFFSET(target->edict());
        if (!current()) return call.Cancel();
        if (!offset || (target->pev->flags & FL_DORMANT)) return true;
        if (state->addonCriticalPending)
        {
            target->HostState().damageEffect = 8;
            state->addonCriticalPending = false;
        }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::RecordCapEvent(const EHANDLE& player)
{
    const auto state = Resolve(player);
    if (!state) return false;
    if (!state->capEventPending)
    {
        state->capEventPending = true;
        state->capEventSequence = ++m_eventSequence;
    }
    return true;
}

bool CCSOLScenarioPlayerRuntime::AcknowledgeCapEvent(const EHANDLE& player,
    std::uint64_t generation, std::uint64_t sequence)
{
    const auto state = Resolve(player);
    if (!state || !state->capEventPending || state->generation != generation ||
        state->capEventSequence != sequence) return false;
    state->capEventPending = false;
    return true;
}

bool CCSOLScenarioPlayerRuntime::SetActiveSkill(const EHANDLE& player,
    std::shared_ptr<CSOLScenarioActiveSkill> skill)
{
    auto keepAlive = shared_from_this();
    const auto state = Resolve(player);
    if (!state) return false;
    auto retired = std::exchange(state->activeSkill, std::move(skill));
    const auto revision = ++state->activeSkillRevision;
    retired.reset();
    return Resolve(player) == state && state->activeSkillRevision == revision;
}

bool CCSOLScenarioPlayerRuntime::ReadKillEntitySource(CSOLMonsterDeathCall& call,
    CBaseEntity& entity, int& value, csol::scenario::ConfigError& error)
{
    value = 0;
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current() ||
        call.Inflictor() != &entity || (entity.pev->flags & FL_KILLME)) return false;
    const bool success = ReadEntitySource(entity, value, error);
    return Active() && call.Current() && call.Inflictor() == &entity &&
        !(entity.pev->flags & FL_KILLME) && success;
}

bool CCSOLScenarioPlayerRuntime::ReadEntitySource(CBaseEntity& entity, int& value,
    csol::scenario::ConfigError& error)
{
    value = 0;
    // Raw v138, not active-weapon/catalog resolution. Callers validate the
    // entity and enclosing operation again after this virtual-call boundary.
    if (const auto* source = dynamic_cast<const CSOLWeaponDamageSource*>(&entity))
    { value = source->DamageWeaponId(); return true; }
    if (typeid(entity) == typeid(CBasePlayer) || typeid(entity) == typeid(CCSOLScenarioMonster)) return true;
    if (typeid(entity) == typeid(CKnife)) { value = WEAPON_KNIFE; return true; }
    if (const auto* weapon = dynamic_cast<const CBasePlayerWeapon*>(&entity))
    {
        if (typeid(*weapon) == typeid(CUSP) || typeid(*weapon) == typeid(CAK47) ||
            CSOL_CannonEmptyMonsterKillCallbacks(*weapon) || CSOL_BeamEmptyMonsterKillCallbacks(*weapon) ||
            CSOL_TKnifeEmptyMonsterKillCallbacks(*weapon))
        { value = weapon->m_iId; return true; }
    }
    const auto weapons = m_session->Weapons();
    if (weapons && weapons->Current()) value = weapons->ProjectileSource(entity);
    if (value) return true;
    error.message = "scenario source entity has no verified source adapter";
    return false;
}

bool CCSOLScenarioPlayerRuntime::RootMonsterKilled(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario root kill has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return Active() && call.Current() && Resolve(handle) == state && call.Attacker() == player; };
    const auto valid = [&](CBaseEntity& entity, bool& result)
    {
        result = false;
        if (!entity.pev || !g_engfuncs.pfnEntOffsetOfPEntity)
        { error.message = "scenario root kill entity validation is unavailable"; return false; }
        const EHANDLE candidate(&entity);
        auto* edict = entity.edict();
        if (!edict) return true;
        const int offset = OFFSET(edict);
        if (!current() || candidate.Get<CBaseEntity>() != &entity) return false;
        result = offset && !(entity.pev->flags & (FL_DORMANT | FL_KILLME));
        return true;
    };
    bool eligible{};
    if (!valid(*player, eligible)) return false;
    if (!eligible) return true;
    const int classification = call.Current()->Classify();
    if (!current()) return false;
    if (classification != 17) return true;
    const auto ability = [&]() -> bool
    {
        auto* inflictor = call.Inflictor();
        if (!inflictor) return true;
        if (!valid(*inflictor, eligible)) return false;
        if (!eligible) return true;
        if (!valid(*player, eligible)) return false;
        if (!eligible) return true;
        if (!g_engfuncs.pfnIndexOfEdict)
        { error.message = "scenario root kill index lookup is unavailable"; return false; }
        const auto sourceCurrent = [&]()
        { return current() && call.Inflictor() == inflictor && !(inflictor->pev->flags & FL_KILLME); };
        if (!sourceCurrent()) return false;
        const int sourceIndex = inflictor->entindex();
        if (!sourceCurrent()) return false;
        const int playerIndex = player->entindex();
        if (!sourceCurrent()) return false;
        int source{};
        if (sourceIndex == playerIndex)
        {
            auto* active = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
            if (active && !(active->pev->flags & FL_KILLME)) source = active->m_iId;
            // An invalid active item does not fall back to the inflictor.
        }
        else if (!ReadKillEntitySource(call, *inflictor, source, error) || !sourceCurrent()) return false;
        if (source != WEAPON_KNIFE) return true;
        bool success = true;
        {
            const auto skill = state->activeSkill;
            const auto revision = state->activeSkillRevision;
            const auto sameSkill = [&]()
            { return current() && state->activeSkill == skill && state->activeSkillRevision == revision; };
            if (skill)
            {
                const int type = skill->Type();
                if (!sameSkill()) return false;
                if (type == 10)
                {
                    bool ready{};
                    success = skill->Ready(call, ready, error);
                    if (!sameSkill() || !success) return false;
                    if (ready) success = skill->Activate(call, error);
                    if (!current()) return false;
                }
            }
        }
        // Releasing a self-replaced skill can itself cancel this transaction.
        return current() && success;
    };
    if (!ability() || !current()) return false;
    return RecordActiveWeaponKill(call, error);
}

bool CCSOLScenarioPlayerRuntime::RecordActiveWeaponKill(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario weapon kill has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return Active() && call.Current() && Resolve(handle) == state && call.Attacker() == player; };
    auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
    if (!weapon || (weapon->pev->flags & FL_KILLME)) return true;
    const auto weapons = m_session->Weapons();
    if (!weapons || !weapons->Current())
    { error.message = "scenario weapon kill catalog is unavailable"; return false; }
    std::int32_t item{};
    // This phase deliberately uses the current weapon, not the lethal source.
    if (!weapons->Item(*player, weapon->m_iId, item, error) || !current()) return false;
    // MP 108A8DE0 rechecks player eligibility after catalog resolution, before
    // its virtual Bot query. A dormant player is skipped, not a failed kill.
    if (player->pev->flags & FL_DORMANT) return true;
    // CSO captures the catalog item before its virtual Bot query. A callback
    // may switch weapons, but this statistic still belongs to that item.
    const bool bot = player->IsBot();
    if (!current()) return false;
    if (!bot && item && !state->profile.weaponEffectsBlocked21F2) ++state->weaponKills[item];
    return true;
}

bool CCSOLScenarioPlayerRuntime::RecordWeaponDamage(CSOLMonsterDamageCall& call, int capturedSource,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this && runtime->session == m_session && Active(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = call.Player();
    if (!player || !call.Inflictor()) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return owned() && call.Current() && Resolve(handle) == state && call.Player() == player; };
    const auto weapons = m_session->Weapons();
    if (!weapons || !weapons->Current())
    { error.message = "scenario weapon damage catalog is unavailable"; return false; }
    // v1D8 captures numeric source at entry, before its upgrade/XP/addon
    // callbacks. Resolve its catalog item here, but never re-read active gun.
    std::int32_t item{};
    if (!weapons->Item(*player, capturedSource, item, error) || !current()) return false;
    if (!item) return true;
    const float damage = call.damage;
    auto* edict = player->pev ? player->pev->pContainingEntity : nullptr;
    if (!edict) return true;
    if (!g_engfuncs.pfnEntOffsetOfPEntity)
    { error.message = "scenario weapon damage identity query is unavailable"; return false; }
    const bool valid = OFFSET(edict) != 0;
    if (!current()) return false;
    if (!valid || (player->pev->flags & FL_DORMANT)) return true;
    const bool bot = player->IsBot();
    if (!current()) return false;
    if (bot || state->profile.weaponEffectsBlocked21F2) return true;
    const auto found = state->weaponDamage.find(item);
    const float previous = found == state->weaponDamage.end() ? 0.0f : found->second;
    const float total = previous + damage;
    if (!std::isfinite(damage) || !std::isfinite(total))
    { error.message = "scenario weapon damage statistic is not finite"; return false; }
    state->weaponDamage[item] = total;
    return true;
}

bool CCSOLScenarioPlayerRuntime::RecordMonsterHeadHit(CSOLMonsterDamageCall& call, int capturedHitgroup,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this && runtime->session == m_session && Active(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return owned() && call.Current() && Resolve(handle) == state && call.Player() == player; };
    auto* monster = call.Current();
    if (capturedHitgroup != HITGROUP_HEAD) return true;
    const int classification = monster->Classify();
    if (!current()) return false;
    if (classification == 17)
    {
        const auto* type = call.Current()->Type();
        if (!type) { error.message = "scenario head-hit target has no monster type"; return false; }
        if (static_cast<std::int16_t>(type->skill) >= 1000) return true;
    }
    else
    {
        const int second = call.Current()->Classify();
        if (!current()) return false;
        if (second == 34) return true;
        const int third = call.Current()->Classify();
        if (!current()) return false;
        if (third == 33) return true;
    }
    // Original INC wraps, then the sender captures that count before resolving
    // its recipient. Nested message callbacks cannot change this packet value.
    const auto count = std::bit_cast<std::int32_t>(++state->monsterHeadHits);
    const bool sent = m_session->Messages().SendMonsterHeadHits(player->edict(), count);
    if (!current()) return false;
    if (!sent) { error.message = "scenario head-hit statistic message failed"; return false; }
    return true;
}

bool CCSOLScenarioPlayerRuntime::AppendKillChainRecord(const CSOLScenarioPlayerRuntimeState& player,
    CSOLScenarioKillChainRecord::Kind kind, csol::scenario::ConfigError& error)
{
    if (m_killChainRecords.size() >= 65536 || m_killChainSequence == UINT64_MAX)
    { error.message = "scenario kill-chain submissions have not been saved"; return false; }
    CSOLScenarioKillChainRecord record;
    record.kind = kind;
    record.sequence = m_killChainSequence + 1;
    record.playerGeneration = player.generation;
    record.mapGeneration = m_session->Map().SpawnPoints().Generation();
    record.roundGeneration = m_session->Map().RoundGeneration();
    // As with the existing kill-position journal, this is a local identity;
    // never send it to the official backend as an account identifier.
    record.localIdentity = static_cast<std::uint32_t>(player.generation);
    m_killChainRecords.push_back(record);
    m_killChainSequence = record.sequence;
    return true;
}

bool CCSOLScenarioPlayerRuntime::AcknowledgeKillChainRecords(std::uint64_t throughSequence)
{
    if (!throughSequence) return false;
    const auto found = std::find_if(m_killChainRecords.begin(), m_killChainRecords.end(),
        [throughSequence](const auto& record) { return record.sequence == throughSequence; });
    if (found == m_killChainRecords.end()) return false;
    m_killChainRecords.erase(m_killChainRecords.begin(), found + 1);
    return true;
}

bool CCSOLScenarioPlayerRuntime::NotifyKillChainRibbon(CSOLMonsterDamageCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    csol::scenario::ConfigError& error)
{
    auto* player = call.Player();
    const auto current = [&]
    {
        return runtime && !runtime->Closing() && runtime->session == m_session &&
            runtime->players.get() == this && Active() && call.Current() &&
            Resolve(state->player) == state && call.Player() == player;
    };
    if (!player || !current()) return call.Cancel();
    const auto eligible = [&](bool& allowed)
    {
        allowed = false;
        if (!current()) return call.Cancel();
        if (!player->pev || !player->pev->pContainingEntity) return true;
        if (!g_engfuncs.pfnEntOffsetOfPEntity)
        { error.message = "scenario kill-chain ribbon identity query is unavailable"; return false; }
        const int offset = OFFSET(player->pev->pContainingEntity);
        if (!current()) return call.Cancel();
        allowed = offset && !(player->pev->flags & FL_DORMANT);
        // Original caller reads dormant again after the common validity gate.
        if (allowed && (player->pev->flags & FL_DORMANT)) allowed = false;
        return true;
    };
    try
    {
        // Mode15 passes the original mode48 gate. Kind6 changes no counters
        // in this service; it clears the candidate list and inserts only44.
        bool allowed{};
        if (!eligible(allowed)) return false;
        if (!allowed) return true;
        if (!g_engfuncs.pfnIndexOfEdict)
        { error.message = "scenario kill-chain ribbon slot query is unavailable"; return false; }
        const int index = ENTINDEX(player->edict());
        if (!current()) return call.Cancel();
        if (index < 1 || index > MAX_CLIENTS)
        { error.message = "scenario kill-chain ribbon slot is outside player capacity"; return false; }

        // 1083B910 independently repeats eligibility and its virtual Bot gate.
        if (!eligible(allowed)) return false;
        if (!allowed) return true;
        const bool bot = player->IsBot() != FALSE;
        if (!current()) return call.Cancel();
        if (bot) return true;
        const auto announcements = m_session->Announcements();
        if (!announcements)
        { error.message = "scenario kill-chain announcement table is unavailable"; return false; }
        if (!announcements->Allowed(15, 44)) return true;
        if (!runtime->room.noRibbon &&
            !AppendKillChainRecord(*state, CSOLScenarioKillChainRecord::Kind::Ribbon44, error)) return false;

        // The mode15 sender remains enabled even with NoRibbon. Recheck the
        // recipient after account submission, as original 1083BE85..BEAE.
        if (!eligible(allowed)) return false;
        if (!allowed) return true;
        const bool sendBot = player->IsBot() != FALSE;
        if (!current()) return call.Cancel();
        if (sendBot) return true;
        constexpr std::array<std::uint8_t, 1> ids{44};
        const bool sent = m_session->Messages().SendAnnounceRibbons(player->edict(), ids);
        if (!current()) return call.Cancel();
        if (!sent) { error.message = "scenario kill-chain ribbon notification failed"; return false; }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::RecordMonsterKillChain(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, int cachedMapId, int currentMapId,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this && runtime->session == m_session && Active(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) { error.message = "scenario kill-chain player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && call.Current() && Resolve(handle) == state && call.Player() == player; };
    auto* monster = call.Current();
    // Ordered COMISS/JB skips unordered health as well as a positive value.
    if (!(monster->pev->health <= 0)) return true;
    try
    {
        const int classification = monster->Classify();
        if (!current()) return call.Cancel();
        if (classification == 17)
        {
            bool boss{};
            if (!csol::scenario::MonsterStatisticsBoss(call.Current()->Type(), cachedMapId, currentMapId, boss, error))
                return false;
            if (boss) return true;
        }
        if (!gpGlobals)
        { error.message = "scenario kill-chain clock is unavailable"; return false; }
        const auto resolvePlayer = [&]
        {
            if (!current()) return call.Cancel();
            // The original statistic owns a client index. Use the slot that
            // Ensure registered, without adding an ENTINDEX callback here.
            const auto registered = std::find(m_players.begin(), m_players.end(), state);
            const int index = static_cast<int>(registered - m_players.begin());
            if (!gpGlobals || registered == m_players.end() || index < 1 || index > gpGlobals->maxClients ||
                !g_engfuncs.pfnPEntityOfEntIndex)
            { error.message = "scenario kill-chain player lookup is unavailable"; return false; }
            auto* entity = INDEXENT(index);
            // INDEXENT is a callback: validate the old operation before reading
            // the returned edict or its private data, and never redirect it to
            // a new player that has occupied the same slot.
            if (!current()) return call.Cancel();
            if (!entity || entity != handle.Get() || entity->free || CBaseEntity::Instance(entity) != player)
                return call.Cancel();
            return true;
        };
        const auto signedCount = [&] { return std::bit_cast<std::int32_t>(state->monsterKillChain); };
        const float window = signedCount() < 30 ? 3.0f : 2.0f;
        const float deadline = state->lastMonsterKillAt + window;
        if (deadline > gpGlobals->time)
        {
            ++state->monsterKillChain; // Defined wrap, followed by signed comparisons.
            const int hundred = signedCount();
            if (hundred > 0 && hundred % 100 == 0)
            {
                if (!resolvePlayer()) return false;
                if (!NotifyKillChainRibbon(call, runtime, state, error)) return false;
            }
            if (!current()) return call.Cancel();
            // The ribbon callbacks may have changed the chain. Do not reuse
            // the earlier hundred-test value for the fifty-test or packet.
            const int fifty = signedCount();
            if (fifty > 0 && fifty % 50 == 0)
            {
                // Original clears the shared buffer then submits C6 with
                // hasBuffer=1. This typed local record has an empty payload;
                // it cannot accidentally inherit bytes from a previous event.
                if (!resolvePlayer()) return false;
                if (!AppendKillChainRecord(*state, CSOLScenarioKillChainRecord::Kind::EventC6, error)) return false;
            }
            // Capture the packet count before the third original resolver;
            // a callback may change the later best/time without changing it.
            const int count = signedCount();
            if (!resolvePlayer()) return false;
            const bool sent = m_session->Messages().SendMonsterKillChain(player->edict(), count);
            if (!current()) return call.Cancel();
            if (!sent) { error.message = "scenario kill-chain notification failed"; return false; }
            // Original reads chain again after MessageEnd before comparing best.
            if (signedCount() > std::bit_cast<std::int32_t>(state->bestMonsterKillChain))
                state->bestMonsterKillChain = state->monsterKillChain;
        }
        else state->monsterKillChain = 0; // This kill does not start a new chain at1.
        if (!gpGlobals)
        { error.message = "scenario kill-chain final clock is unavailable"; return false; }
        state->lastMonsterKillAt = gpGlobals->time;
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::ResolveDamageStatisticPlayer(
    CSOLMonsterDamageCall& call, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    csol::scenario::ConfigError& error)
{
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this &&
        runtime->session == m_session && Active() && call.Current(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map())) return {};
    auto* player = call.Player();
    if (!player) return {};
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) { error.message = "scenario damage statistic player state is unavailable"; return {}; }
    const auto current = [&]
    { return owned() && Resolve(handle) == state && call.Player() == player; };
    if (!g_engfuncs.pfnIndexOfEdict)
    { error.message = "scenario damage statistic slot query is unavailable"; return {}; }
    try
    {
        // Original 108F88B0 performs ENTINDEX before reading the state slot.
        // Recheck the connection after the engine callback, not only before it.
        const int index = ENTINDEX(player->edict());
        if (!current()) { call.Cancel(); return {}; }
        if (index < 1 || index >= static_cast<int>(m_players.size()) || m_players[index] != state)
        { error.message = "scenario damage statistic slot does not match its player"; return {}; }
        return state;
    }
    catch (...)
    {
        if (!current()) { call.Cancel(); return {}; }
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::NotifyActiveSkillMonsterHit(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    if (!runtime || runtime->Closing() || runtime->players.get() != this || runtime->session != m_session ||
        !Active() || !call.Spawn().BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = call.Player();
    if (!player) return true;
    const auto state = ResolveDamageStatisticPlayer(call, runtime, error);
    if (!state) return false;
    const auto current = [&]
    { return !runtime->Closing() && runtime->players.get() == this && runtime->session == m_session &&
        Active() && call.Current() && Resolve(state->player) == state && call.Player() == player; };
    try
    {
        bool success = true;
        {
            // The outer type12 path keeps this one object across Type, v10
            // and the area effect even if a callback registers a new ability.
            const auto skill = state->activeSkill;
            if (skill)
            {
                const int type = skill->Type();
                if (!current()) return call.Cancel();
                if (type == 12)
                {
                    auto* damageSkill = dynamic_cast<CSOLScenarioDamageSkill*>(skill.get());
                    if (!damageSkill)
                    { error.message = "scenario type12 damage ability is not implemented"; success = false; }
                    else
                    {
                        bool active{};
                        success = damageSkill->ActiveForMonsterHit(call, active, error);
                        if (!current()) return call.Cancel();
                        if (success && error.message.empty() && active)
                            success = damageSkill->ActiveMonsterHit(call, error);
                    }
                }
            }
        }
        // Destroying a self-replaced object may itself retire the player.
        if (!current()) return call.Cancel();
        return success && error.message.empty();
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::RecordMonsterDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, bool boss, float amount,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    const auto keepAlive = shared_from_this();
    if (!runtime || runtime->Closing() || runtime->players.get() != this || runtime->session != m_session ||
        !Active() || !call.Spawn().BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = call.Player();
    if (!player) return true;
    const auto state = ResolveDamageStatisticPlayer(call, runtime, error);
    if (!state) return false;
    const auto current = [&]
    { return !runtime->Closing() && runtime->players.get() == this && runtime->session == m_session &&
        Active() && call.Current() && Resolve(state->player) == state && call.Player() == player; };
    try
    {
        float& total = boss ? state->bossDamage : state->ordinaryMonsterDamage;
        const float updated = amount + total;
        ++state->monsterDamageHits;
        const bool hasSkill = state->activeSkill != nullptr;
        total = updated;
        if (!hasSkill) return true;

        // Only one original virtual call owns each pin. Release it and check
        // lifetime before re-reading +D8 for the next call. Replacement is a
        // legitimate state change; clearing a required later object is not an
        // empty initial registry and must not silently skip the remaining work.
        const auto invoke = [&](auto&& callback, bool optional = false)
        {
            bool success{};
            {
                const auto skill = state->activeSkill;
                if (!skill)
                {
                    if (optional) return true;
                    error.message = "scenario damage ability was cleared before a required callback";
                    return false;
                }
                success = callback(*skill);
            }
            if (!current()) return call.Cancel();
            return success && error.message.empty();
        };
        const auto damageSkill = [&](CSOLScenarioActiveSkill& skill) -> CSOLScenarioDamageSkill*
        {
            auto* result = dynamic_cast<CSOLScenarioDamageSkill*>(&skill);
            if (!result) error.message = "scenario active ability has no damage implementation";
            return result;
        };
        if (boss)
        {
            bool automatic{};
            if (!invoke([&](auto& skill)
                { auto* damage = damageSkill(skill); return damage && damage->AutoActivateOnBossDamage(call, automatic, error); }))
                return false;
            if (automatic)
            {
                int type{};
                const auto readType = [&](auto& skill) { type = skill.Type(); return true; };
                if (!invoke(readType)) return false;
                bool activate = type == 11;
                if (!activate)
                {
                    if (!invoke(readType)) return false;
                    activate = type == 13;
                }
                if (activate && !invoke([&](auto& skill)
                    { auto* damage = damageSkill(skill); return damage && damage->ActivateOnBossDamage(call, error); }, true))
                    return false;
            }
        }
        if (!invoke([&](auto& skill)
            {
                auto* damage = damageSkill(skill);
                return damage && (boss ? damage->BossDamaged(call, amount, error) :
                    damage->MonsterDamaged(call, amount, error));
            })) return false;
        bool progress{};
        if (!invoke([&](auto& skill)
            { auto* damage = damageSkill(skill); return damage && damage->UsesDamageProgress(call, progress, error); }))
            return false;
        if (progress && !invoke([&](auto& skill)
            { auto* damage = damageSkill(skill); return damage && damage->AdvanceDamageProgress(call, error); }))
            return false;
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::NotifyMonsterHitComponent(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this &&
        runtime->session == m_session && Active() && call.Current(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map())) return false;
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) { error.message = "scenario hit component player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && Resolve(handle) == state && call.Player() == player; };
    std::shared_ptr<CSOLScenarioDisplayAbilities> abilities;
    std::shared_ptr<csol::scenario::Feature3> feature;
    const auto featureCurrent = [&]
    { return current() && (!feature || (state->displayAbilities == abilities &&
        abilities->feature3 == feature && feature->Valid())); };
    const auto resolvePlayer = [&]
    {
        const auto registered = std::find(m_players.begin(), m_players.end(), state);
        const int index = static_cast<int>(registered - m_players.begin());
        if (!gpGlobals || registered == m_players.end() || index < 1 || index > gpGlobals->maxClients ||
            !g_engfuncs.pfnPEntityOfEntIndex)
        { error.message = "scenario hit component player lookup is unavailable"; return false; }
        auto* entity = INDEXENT(index);
        if (!current()) return call.Cancel();
        if (!entity || entity != handle.Get() || entity->free || CBaseEntity::Instance(entity) != player)
            return call.Cancel();
        return true;
    };
    try
    {
        // 108F64E0 resolves the statistic's slot three times, with OFFSET and
        // the source virtual between them. Do not introduce an ENTINDEX here.
        if (!resolvePlayer()) return false;
        auto* inflictor = call.Inflictor();
        if (!inflictor || !inflictor->pev) return true;
        auto* sourcePev = inflictor->pev;
        if (!g_engfuncs.pfnEntOffsetOfPEntity)
        { error.message = "scenario hit component inflictor validation is unavailable"; return false; }
        const int offset = OFFSET(sourcePev->pContainingEntity);
        if (!current()) return call.Cancel();
        if (!offset) return true;
        // Context is read after OFFSET, exactly once. The raw source callback
        // may subsequently change it; that does not repeat this gate.
        if (call.context.bytes[2] || call.context.bytes[20] || call.context.bytes[7]) return true;
        if (!resolvePlayer()) return false;
        if (sourcePev != player->pev)
        {
            // Current() protects the original EHANDLE, but also reject a
            // re-pointed pev/container rather than invoking another entity.
            if (call.Inflictor() != inflictor || inflictor->pev != sourcePev ||
                !sourcePev->pContainingEntity ||
                CBaseEntity::Instance(sourcePev->pContainingEntity) != inflictor)
            { error.message = "scenario hit component inflictor private data is unavailable"; return false; }
            int source{};
            const bool resolved = ReadEntitySource(*inflictor, source, error);
            if (!current()) return call.Cancel();
            if (!resolved || !error.message.empty()) return false;
            if (!source) return true;
        }
        if (!resolvePlayer()) return false;
        abilities = state->displayAbilities;
        if (!abilities) { error.message = "scenario hit component directory is unavailable"; return false; }
        feature = abilities->feature3;
        if (!feature) return true;
        const csol::scenario::Feature3Query query{
            [](const void* context) { return (*static_cast<const decltype(featureCurrent)*>(context))(); }, &featureCurrent,
            [](const void*, float& now)
            {
                if (!gpGlobals) return false;
                now = gpGlobals->time;
                return true;
            }, nullptr};
        bool changed{};
        const bool stopped = feature->Stop(0x0E, false, query, changed, error);
        if (!featureCurrent() || (!stopped && error.message.empty())) return call.Cancel();
        if (!stopped || !error.message.empty()) return false;
        if (!changed) return true;

        // This owner is specifically the mode15 scenario service. Original
        // mode45/55 has an additional state effect; it is not used in mode15.
        // The kind3 lookup ignores enabled, but the following module broadcast
        // observes each module's current enabled flag and live successor.
        if (m_componentDispatchDepth >= 32)
        { error.message = "scenario nested cloaking notification limit exceeded"; return false; }
        ++m_componentDispatchDepth;
        struct DispatchScope { std::size_t& depth; ~DispatchScope() { --depth; } } scope{m_componentDispatchDepth};
        std::uint64_t previous{};
        std::size_t count{};
        for (;;)
        {
            const auto next = std::find_if(state->killComponents.begin(), state->killComponents.end(),
                [previous](const auto& entry) { return entry.token > previous; });
            if (next == state->killComponents.end()) break;
            previous = next->token;
            if (!next->enabled) continue;
            if (++count > 4096)
            { error.message = "scenario cloaking notification registration limit exceeded"; return false; }
            bool success{};
            {
                const auto component = next->component;
                auto* observer = dynamic_cast<CSOLScenarioCloakingObserver*>(component.get());
                if (!observer)
                { error.message = "scenario player module has no cloaking notification implementation"; return false; }
                success = observer->CloakingChanged(call, error);
            }
            if (!featureCurrent()) return call.Cancel();
            if (!success || !error.message.empty()) return false;
        }
        return true;
    }
    catch (...)
    {
        if (!featureCurrent()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::NotifyClassModMonsterDamage(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this &&
        runtime->session == m_session && Active() && call.Current(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map())) return call.Cancel();
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) { error.message = "scenario ClassMod damage player state is unavailable"; return false; }
    const auto current = [&]
    { return owned() && Resolve(handle) == state && call.Player() == player; };
    // 114E9CB0 tests equality with1, unlike the nonzero weapon-effect gates.
    if (state->profile.weaponEffectsBlocked21F2 == 1) return true;
    std::optional<csol::scenario::ClassModDefinition> selected;
    if (!CurrentClassMod(handle, 2, selected, error))
        return current() ? false : call.Cancel();
    if (!current()) return call.Cancel();
    if (!selected) return true;
    if (!g_engfuncs.pfnIndexOfEdict)
    { error.message = "scenario ClassMod damage player index query is unavailable"; return false; }
    // The original argument is by value and has no positive-damage gate.
    // Match cvttss2si's indefinite INT32_MIN result without a C++ overflow cast.
    const float damage = call.damage;
    const std::int32_t amount = static_cast<double>(damage) >= -2147483648.0 &&
        static_cast<double>(damage) < 2147483648.0 ? static_cast<std::int32_t>(damage) : INT32_MIN;
    const auto messages = m_session->Messages();
    auto* recipient = handle.Get();
    try
    {
        const int index = ENTINDEX(recipient);
        if (!current()) return call.Cancel();
        if (index < 1 || index > MAX_CLIENTS)
        { error.message = "scenario ClassMod damage player index is outside player capacity"; return false; }
        const bool sent = messages.SendClassModMonsterDamage(recipient, index, amount);
        if (!current()) return call.Cancel();
        if (!sent) { error.message = "scenario ClassMod damage notification failed"; return false; }
        return true;
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::SetMonsterHitEvents(std::shared_ptr<CSOLScenarioMonsterHitEvents> events)
{
    const auto keepAlive = shared_from_this();
    if (!Active()) return false;
    const auto lifecycle = m_lifecycle;
    auto retired = std::exchange(m_monsterHitEvents, events);
    retired.reset();
    return Active() && m_lifecycle == lifecycle && m_monsterHitEvents == events;
}

bool CCSOLScenarioPlayerRuntime::NotifyMonsterHitEvent(CSOLMonsterDamageCall& call,
    std::shared_ptr<CCSOLScenarioRuntime> runtime, csol::scenario::ConfigError& error)
{
    const auto keepAlive = shared_from_this();
    const auto owned = [&]
    { return runtime && !runtime->Closing() && runtime->players.get() == this &&
        runtime->session == m_session && Active() && call.Current(); };
    if (!owned() || !call.Spawn().BelongsTo(m_session->Map())) return false;
    auto* player = call.Player();
    if (!player) return true;
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    const auto current = [&]
    { return owned() && state && Resolve(handle) == state && call.Player() == player; };
    try
    {
        bool success{};
        {
            // Capture before ENTINDEX. Replacing the service during the
            // callback does not redirect this event to the newly installed one.
            const auto events = m_monsterHitEvents;
            if (!events) return true;
            if (!state) { error.message = "scenario hit event player state is unavailable"; return false; }
            CSOLScenarioMonsterHitEvent value{{true, handle}, state->generation,
                m_session->Map().SpawnPoints().Generation(), m_session->Map().RoundGeneration()};
            if (!g_engfuncs.pfnIndexOfEdict)
            { error.message = "scenario hit event player index query is unavailable"; return false; }
            const int index = ENTINDEX(player->edict());
            if (!current()) return call.Cancel();
            if (index < 1 || index >= static_cast<int>(m_players.size()) || m_players[index] != state)
            { error.message = "scenario hit event slot does not match its player"; return false; }
            success = events->Submit(index, 7, value, error);
        }
        // Includes destruction of the service if it replaced itself.
        if (!current()) return call.Cancel();
        return success && error.message.empty();
    }
    catch (...)
    {
        if (!current()) return call.Cancel();
        throw;
    }
}

bool CCSOLScenarioPlayerRuntime::AddMonsterFrag(CSOLMonsterDeathCall& call, bool& incremented,
    csol::scenario::ConfigError& error)
{
    #pragma clang fp contract(off)
    auto keepAlive = shared_from_this();
    incremented = false;
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario frag has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto current = [&]
    { return Active() && call.Current() && Resolve(handle) == state && call.Attacker() == player; };
    const float before = player->pev->frags;
    const bool add = before < 65000.0f;
    if (add) player->pev->frags = before + 1.0f;
    incremented = add;
    const int index = player->entindex();
    if (!current()) return false;
    if (index < 1 || index > 32)
    { error.message = "scenario frag player index is invalid"; return false; }
    // Index lookup is an engine callback. As in CSO, sample frags afterwards.
    const double integer = std::trunc(static_cast<double>(player->pev->frags));
    if (!std::isfinite(integer) || integer < std::numeric_limits<std::int32_t>::min() ||
        integer > std::numeric_limits<std::int32_t>::max())
    { error.message = "scenario frags cannot be represented by the original message conversion"; return false; }
    // Even a capped value is broadcast. Keep the comparison/add separate;
    // min(before + 1, 65000) changes the original noninteger boundary.
    const auto messages = m_session->Messages();
    if (!messages.SendPlayerFrags(index, static_cast<std::int32_t>(integer)))
    { if (current()) error.message = "scenario frag notification failed"; return false; }
    return current();
}

bool CCSOLScenarioPlayerRuntime::ResolveKillSource(CSOLMonsterDeathCall& call,
    CSOLScenarioKillSourcePhase phase, CSOLScenarioKillSource& output,
    csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    output = {};
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario kill source has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto generation = state->generation;
    const auto weapons = m_session->Weapons();
    const auto current = [&]
    {
        return Active() && call.Current() && Resolve(handle) == state &&
            state->generation == generation && call.Attacker() == player;
    };
    if (phase != CSOLScenarioKillSourcePhase::Attribution && phase != CSOLScenarioKillSourcePhase::Notification)
    { error.message = "scenario kill source phase is invalid"; return false; }
    if (phase == CSOLScenarioKillSourcePhase::Notification && call.Context().bytes[20]) return true;
    auto* inflictor = call.Inflictor();
    if (!inflictor) return current();
    const auto sourceCurrent = [&]
    {
        return current() && call.Inflictor() == inflictor && !(inflictor->pev->flags & FL_KILLME);
    };
    if (!sourceCurrent()) return false;
    const bool isPlayer = inflictor->IsPlayer() != FALSE;
    if (!sourceCurrent()) return false;
    CSOLScenarioKillSource candidate;
    bool selected{};
    if (isPlayer)
    {
        // The source can be another player: use the ATTACKER's current item.
        // A missing active handle falls through to the source's v138 adapter.
        auto* active = dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem);
        if (active && !(active->pev->flags & FL_KILLME))
        { candidate.weapon = active->m_iId; selected = true; }
    }
    const auto readSource = [&](int& value)
    {
        return ReadKillEntitySource(call, *inflictor, value, error);
    };
    if (!selected)
    {
        int probe{};
        if (!readSource(probe) || !sourceCurrent()) return false;
        if (!probe) return true;
        // The first result is a predicate only. The second result may be zero.
        if (!readSource(candidate.weapon) || !sourceCurrent()) return false;
    }
    if (phase == CSOLScenarioKillSourcePhase::Attribution)
    {
        if (!weapons || !weapons->Current())
        { error.message = "scenario kill source catalog is unavailable"; return false; }
        if (!weapons->Item(*player, candidate.weapon, candidate.item, error) || !sourceCurrent() ||
            m_session->Weapons() != weapons || !weapons->Current()) return false;
    }
    output = candidate;
    return true;
}

bool CCSOLScenarioPlayerRuntime::ClassifyMonsterKill(CSOLMonsterDeathCall& call,
    int cachedMapId, int currentMapId, csol::scenario::AnnounceCandidates& output,
    csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    output = {};
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current()) return false;
    bool boss{};
    if (!csol::scenario::MonsterStatisticsBoss(call.Current()->Type(), cachedMapId, currentMapId, boss, error))
        return false;
    return ClassifyKill(call, boss ? 2 : 1, output, error);
}

bool CCSOLScenarioPlayerRuntime::ClassifyKill(CSOLMonsterDeathCall& call, int kind,
    csol::scenario::AnnounceCandidates& output, csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    output = {};
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario classification has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    if (player->pev->flags & FL_DORMANT) return true;
    const auto candidates = state->classification.Record(kind);
    const bool bot = player->IsBot();
    if (!Active() || !call.Current() || Resolve(handle) != state || call.Attacker() != player) return false;
    // Original counters (including C/D resets) have already changed for Bots.
    if (bot || (player->pev->flags & FL_DORMANT)) return true;
    const auto table = m_session->Announcements();
    if (!table) { error.message = "scenario announce table is unavailable"; return false; }
    for (const auto id : candidates.Values())
        if (table->Allowed(15, id)) output.ids[output.count++] = id;
    return true;
}

std::uint64_t CCSOLScenarioPlayerRuntime::RegisterKillComponent(const EHANDLE& player,
    std::shared_ptr<CSOLScenarioKillComponent> component)
{
    auto keepAlive = shared_from_this();
    const auto state = Resolve(player);
    if (!state || !component) return 0;
    for (const auto& entry : state->killComponents)
        if (entry.component == component) return entry.token;
    if (state->killComponents.size() >= 256 || m_killSubscriptionSequence == UINT64_MAX) return 0;
    const auto token = ++m_killSubscriptionSequence;
    state->killComponents.push_back({token, true, std::move(component)});
    return token;
}

bool CCSOLScenarioPlayerRuntime::SetKillComponentEnabled(const EHANDLE& player,
    std::uint64_t token, bool enabled)
{
    const auto state = Resolve(player);
    if (!state || !token) return false;
    for (auto& entry : state->killComponents)
        if (entry.token == token) { entry.enabled = enabled; return true; }
    return false;
}

bool CCSOLScenarioPlayerRuntime::RemoveKillComponent(const EHANDLE& player, std::uint64_t token)
{
    auto keepAlive = shared_from_this();
    const auto state = Resolve(player);
    if (!state || !token) return false;
    for (auto it = state->killComponents.begin(); it != state->killComponents.end(); ++it)
    {
        if (it->token != token) continue;
        // Erase before releasing the callable: its destructor can reenter.
        auto retained = std::move(it->component);
        state->killComponents.erase(it);
        return true;
    }
    return false;
}

bool CCSOLScenarioPlayerRuntime::NotifyMonsterKill(CSOLMonsterDeathCall& call,
    std::int32_t source, csol::scenario::ConfigError& error)
{
    auto keepAlive = shared_from_this();
    if (!Active() || !call.BelongsTo(m_session->Map()) || !call.Current()) return false;
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player) { error.message = "scenario kill notification has no player attacker"; return false; }
    const EHANDLE handle(player);
    const auto state = Resolve(handle);
    if (!state) return false;
    const auto generation = state->generation;
    const auto current = [&]
    {
        return Active() && call.Current() && Resolve(handle) == state &&
            state->generation == generation && call.Attacker() == player;
    };
    if (m_componentDispatchDepth >= 32)
    { error.message = "scenario nested kill notification limit exceeded"; return false; }
    ++m_componentDispatchDepth;
    struct DispatchScope { std::size_t& depth; ~DispatchScope() { --depth; } } scope{m_componentDispatchDepth};
    std::vector<std::uint64_t> tokens;
    tokens.reserve(state->killComponents.size());
    for (const auto& entry : state->killComponents) tokens.push_back(entry.token);
    for (const auto token : tokens)
    {
        bool success = true;
        {
            std::shared_ptr<CSOLScenarioKillComponent> component;
            for (const auto& entry : state->killComponents)
                if (entry.token == token && entry.enabled) { component = entry.component; break; }
            if (component) success = component->MonsterKilled(call, source, error);
        }
        // Includes possible destruction of a self-unregistered component.
        if (!current() || !success) return false;
    }
    const auto notifyWeapon = [&](CBasePlayerWeapon* weapon, bool active)
    {
        if (!weapon || (weapon->pev->flags & FL_KILLME)) return true;
        if (weapon->m_pPlayer != player)
        { error.message = "scenario kill notification weapon owner is invalid"; return false; }
        if (auto* callbacks = dynamic_cast<CSOLWeaponMonsterKillCallbacks*>(weapon))
        {
            auto* monster = call.Current();
            if (!monster) return false;
            if (active) callbacks->ActiveMonsterKilled(*monster, call.Inflictor());
            else callbacks->SourceMonsterKilled(*monster, call.Inflictor());
            return current();
        }
        // Original ret 8 methods have no result. This bool reports adapter
        // coverage, not a fabricated return value from the original method.
        if (typeid(*weapon) == typeid(CKnife) || typeid(*weapon) == typeid(CUSP) ||
            typeid(*weapon) == typeid(CAK47) || CSOL_CannonEmptyMonsterKillCallbacks(*weapon) ||
            CSOL_BeamEmptyMonsterKillCallbacks(*weapon) || CSOL_TKnifeEmptyMonsterKillCallbacks(*weapon)) return true;
        error.message = active ? "scenario active weapon kill callback is unsupported" :
            "scenario source weapon kill callback is unsupported";
        return false;
    };
    // Components may have changed the active item. Each later slot is also
    // resolved at its own turn; do not flatten the grenade chain into this loop.
    if (!notifyWeapon(dynamic_cast<CBasePlayerWeapon*>(player->m_pActiveItem), true) || !current()) return false;
    static_assert(MAX_ITEM_TYPES >= 6);
    for (int slot = 0; slot < 6; ++slot)
    {
        auto* weapon = dynamic_cast<CBasePlayerWeapon*>(player->m_rgpPlayerItems[slot]);
        if (weapon && !(weapon->pev->flags & FL_KILLME) && weapon->m_iId == source &&
            !notifyWeapon(weapon, false)) return false;
        if (!current()) return false;
    }
    CSOLScenarioKillPosition position;
    position.origin = {player->pev->origin.x, player->pev->origin.y, player->pev->origin.z};
    const bool isPlayer = player->IsPlayer() != FALSE;
    if (!current()) return false;
    position.player = isPlayer;
    position.localIdentity = static_cast<std::uint32_t>(generation);
    position.team = static_cast<std::uint16_t>(player->m_iTeam);
    position.source = static_cast<std::uint16_t>(source);
    position.playerGeneration = generation;
    position.mapGeneration = m_session->Map().SpawnPoints().Generation();
    position.roundGeneration = m_session->Map().RoundGeneration();
    if (m_killPositions.size() >= 65536 || m_killPositionSequence == UINT64_MAX)
    { error.message = "scenario kill position consumer has not drained its pending records"; return false; }
    position.sequence = ++m_killPositionSequence;
    m_killPositions.push_back(position);
    return true;
}

bool CCSOLScenarioPlayerRuntime::AcknowledgeKillPositions(std::uint64_t throughSequence)
{
    if (!Active() || !throughSequence) return false;
    const auto found = std::find_if(m_killPositions.begin(), m_killPositions.end(),
        [throughSequence](const auto& position) { return position.sequence == throughSequence; });
    if (found == m_killPositions.end()) return false;
    m_killPositions.erase(m_killPositions.begin(), found + 1);
    return true;
}
