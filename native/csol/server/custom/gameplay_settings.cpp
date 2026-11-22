// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "gameplay_settings.h"
#include "settings_store.hpp"
#include "body_stains.h"
#include "../buy_menu.h"
#include "../../../guest/cof/light_equipment.hpp"
#include "../game_mode.h"
#include "../scenario/core/human_kick.h"
#include "../scenario/core/game_rules_gate.h"
#include "goldcraft/csol_gameplay_settings_api.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

namespace
{
namespace custom = csol::scenario::custom;
constexpr std::size_t Count = custom::FeatureCount + custom::SettingCount;
std::unique_ptr<custom::SettingsStore> store;
custom::SnapshotPtr current;
std::array<cvar_t, Count> variables;
std::array<std::string, Count> names, initial;
bool initialized{}, registered{}, publishing{};
csol::GameplayEditResult lastResult{csol::GameplayEditResult::Unavailable};
std::string failure;
bool Available(custom::Feature feature)
{
    if (feature == custom::Feature::Blood) return CSOL_BodyStainDeltaReady();
    // Consumers register their capability as they are integrated. Publishing
    // a configuration flag alone cannot claim an unimplemented gameplay path.
    return feature == custom::Feature::Kick || feature == custom::Feature::TimeCycle || feature == custom::Feature::BlindBox || feature == custom::Feature::DamageRefundCap ||
        feature == custom::Feature::NightPlayerLights || feature == custom::Feature::CofSprint ||
        feature == custom::Feature::DuskBuff || feature == custom::Feature::NightBuff ||
        feature == custom::Feature::ZombiePopulation || feature == custom::Feature::RandomHorde ||
        feature == custom::Feature::ZombieGibs || feature == custom::Feature::NightEquipment ||
        feature == custom::Feature::AmmoRefill;
}

std::optional<double> LiveNumber(custom::Setting id)
{
    const auto index = static_cast<std::size_t>(id);
    const auto& descriptor = custom::Settings()[index];
    const double value = variables[custom::FeatureCount + index].value;
    if (!publishing && value >= descriptor.minimum && value <= descriptor.maximum &&
        (descriptor.type != custom::ValueType::Integer || std::trunc(value) == value))
        return value;
    return current->Number(id);
}

template<std::size_t N> void Copy(char (&output)[N], std::string_view value)
{
    const auto size = std::min(value.size(), N - 1);
    std::memcpy(output, value.data(), size); output[size] = 0;
}
std::string Number(double value)
{
    std::array<char, 64> text{};
    const auto converted = std::to_chars(text.data(), text.data() + text.size(), value,
                                        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    return converted.ec == std::errc{} ? std::string(text.data(), converted.ptr) : std::string{};
}
void Publish(const custom::SnapshotPtr& snapshot)
{
    if (!snapshot || publishing) return;
    publishing = true;
    for (const auto& descriptor : custom::Features())
    {
        const auto index = static_cast<std::size_t>(descriptor.id);
        CVAR_SET_FLOAT(names[index].c_str(), snapshot->Enabled(descriptor.id) ? 1 : 0);
    }
    for (const auto& descriptor : custom::Settings())
    {
        const auto index = custom::FeatureCount + static_cast<std::size_t>(descriptor.id);
        const auto value = snapshot->Number(descriptor.id);
        const auto text = value ? Number(*value) : std::string("-1");
        CVAR_SET_STRING(names[index].c_str(), text.c_str());
    }
    CSOL_PrepareBlindBoxPools(*snapshot);
    current = snapshot;
    CSOL_SetHumanKickPolicy(snapshot->Enabled(custom::Feature::Kick));
    publishing = false;
}
void Poll()
{
    if (!store || publishing) return;
    const auto result = store->Poll();
    if (!result) return;
    if (result->success)
    {
        Publish(result->snapshot);
        lastResult = csol::GameplayEditResult::Saved;
        failure.clear();
    }
    else
    {
        failure = result->error.message;
        lastResult = csol::GameplayEditResult::Failed;
        ALERT(at_error, "CSOL gameplay settings were not saved: %s\n", failure.c_str());
    }
}
class Service final : public csol::IGameplaySettingsService
{
public:
    bool Status(csol::GameplaySettingsStatus& output) override
    {
        Poll();
        output = {};
        if (!store || !current) { Copy(output.error, failure); return false; }
        if (store->Revision() > UINT32_MAX) { Copy(output.error, "settings revision exhausted"); return false; }
        output.revision = static_cast<std::uint32_t>(store->Revision());
        output.count = static_cast<int>(Count);
        output.busy = store->Busy() || publishing;
        output.result = lastResult;
        Copy(output.error, failure);
        return true;
    }
    bool Read(int id, csol::GameplaySetting& output) override
    {
        Poll();
        if (!current || id < 0 || id >= static_cast<int>(Count)) return false;
        output = {}; output.id = id;
        if (id < static_cast<int>(custom::FeatureCount))
        {
            const auto& descriptor = custom::Features()[id];
            output.type = csol::GameplayValueType::Boolean;
            output.value = current->Enabled(descriptor.id) ? 1 : 0;
            output.minimum = 0; output.maximum = 1; output.available = Available(descriptor.id);
            output.nextMap = descriptor.scope == custom::Scope::NextMapSession;
            Copy(output.key, descriptor.key); Copy(output.cvar, descriptor.cvar);
            Copy(output.comment, current->Comment(descriptor.id));
        }
        else
        {
            const auto& descriptor = custom::Settings()[id - custom::FeatureCount];
            output.type = descriptor.type == custom::ValueType::Integer ?
                csol::GameplayValueType::Integer : csol::GameplayValueType::Real;
            const auto value = current->Number(descriptor.id);
            output.value = value.value_or(0); output.available = value.has_value();
            output.minimum = descriptor.minimum; output.maximum = descriptor.maximum;
            output.nextMap = descriptor.scope == custom::Scope::NextMapSession;
            Copy(output.key, descriptor.key); Copy(output.cvar, descriptor.cvar);
            Copy(output.comment, current->Comment(descriptor.id));
        }
        return true;
    }
    int UserId(int index) const override
    {
        if (CSOL_LockedGameMode() != 2 || !gpGlobals || index < 1 || index > gpGlobals->maxClients ||
            !g_engfuncs.pfnPEntityOfEntIndex || !g_engfuncs.pfnGetPlayerUserId) return 0;
        auto* entity = INDEXENT(index);
        if (!entity || entity->free || !(entity->v.flags & FL_CLIENT) ||
            (entity->v.flags & (FL_FAKECLIENT | FL_DORMANT | FL_KILLME))) return 0;
        return GETPLAYERUSERID(entity);
    }
    csol::GameplayEditResult Edit(int actor, int userid, std::uint32_t revision, int id, double value) override
    {
        Poll();
        if (userid <= 0 || UserId(actor) != userid) return csol::GameplayEditResult::Failed;
        if (!store || !current) return csol::GameplayEditResult::Unavailable;
        if (store->Busy() || publishing) return csol::GameplayEditResult::Busy;
        if (revision != store->Revision()) return csol::GameplayEditResult::Stale;
        if (id < 0 || id >= static_cast<int>(Count) || !std::isfinite(value))
            return csol::GameplayEditResult::Failed;
        custom::Draft draft(*current);
        custom::Error validation;
        bool changed{};
        if (id < static_cast<int>(custom::FeatureCount))
        {
            if (value != 0 && value != 1) return csol::GameplayEditResult::Failed;
            if (value && !Available(custom::Features()[id].id))
            { failure = "This gameplay feature is not installed yet"; return csol::GameplayEditResult::Unavailable; }
            if (value && id == int(custom::Feature::TimeCycle) && !CSOL_ScenarioTimeCycleReady())
            { failure = "Time-cycle resources or engine service were unavailable at map startup"; return csol::GameplayEditResult::Unavailable; }
            changed = draft.Set(custom::Features()[id].id, value != 0, validation);
        }
        else changed = draft.Set(custom::Settings()[id - custom::FeatureCount].id, value, validation);
        custom::SnapshotPtr candidate;
        if (!changed || !draft.Finish(candidate, validation))
        { failure = validation.field + ": " + validation.message; return csol::GameplayEditResult::Failed; }
        custom::StoreError error;
        if (UserId(actor) != userid) return csol::GameplayEditResult::Failed;
        if (!store->Submit(revision, candidate, error))
        { failure = error.message; return csol::GameplayEditResult::Failed; }
        lastResult = csol::GameplayEditResult::Queued;
        failure.clear();
        return lastResult;
    }
} service;
}

using csol::IGameplaySettingsService;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(Service, IGameplaySettingsService, csol::GameplaySettingsServiceVersion, service);

void CSOL_InitGameplaySettings()
{
    if (initialized) return;
    initialized = true;
    CSOL_SetHumanKickPolicy(false);
    if (!g_engfuncs.pfnGetGameDir)
    { failure = "engine game-directory API is unavailable"; return; }
    char directory[4096]{};
    GET_GAME_DIR(directory);
    custom::StoreError error;
    store = custom::SettingsStore::Open(std::filesystem::absolute(directory), error);
    if (!store)
    {
        failure = error.message;
        ALERT(at_error, "CSOL gameplay settings initialization failed: %s\n", failure.c_str());
        return;
    }
    current = store->Snapshot();
    CSOL_PrepareBlindBoxPools(*current);
    for (const auto& descriptor : custom::Features())
    {
        const auto index = static_cast<std::size_t>(descriptor.id);
        names[index] = descriptor.cvar;
        initial[index] = current->Enabled(descriptor.id) ? "1" : "0";
    }
    for (const auto& descriptor : custom::Settings())
    {
        const auto index = custom::FeatureCount + static_cast<std::size_t>(descriptor.id);
        names[index] = descriptor.cvar;
        const auto value = current->Number(descriptor.id);
        initial[index] = value ? Number(*value) : "-1";
    }
    for (std::size_t index = 0; index < Count; ++index)
    {
        variables[index] = {const_cast<char*>(names[index].c_str()), const_cast<char*>(initial[index].c_str()), FCVAR_SERVER, 0, nullptr};
        CVAR_REGISTER(&variables[index]);
    }
    registered = true;
    lastResult = csol::GameplayEditResult::Saved;
    CSOL_SetHumanKickPolicy(current->Enabled(custom::Feature::Kick));
}
void CSOL_PollGameplaySettings()
{
    Poll();
    if (registered) CSOL_SetHumanKickPolicy(CSOL_CustomFeatureEnabled(custom::Feature::Kick));
}
void CSOL_UpdateGameplayBodyStains() noexcept
{
    const bool enabled = CSOL_CustomFeatureEnabled(custom::Feature::Blood);
    CSOL_SetBodyStainPolicy(enabled, enabled ?
        static_cast<float>(LiveNumber(custom::Setting::BloodSplashRadius).value_or(0)) : 0.0f);
}

void CSOL_ShutdownGameplaySettings()
{
    // Process shutdown is a cold boundary; drain an authorized file write
    // before CRT/loader-lock destruction and before the engine filesystem ends.
    store.reset(); current.reset(); registered = false;
    CSOL_SetHumanKickPolicy(false);
}
custom::SnapshotPtr CSOL_CustomSettings() { return current; }
bool CSOL_CustomFeatureEnabled(custom::Feature feature)
{
    const auto index = static_cast<std::size_t>(feature);
    return registered && current && index < custom::FeatureCount && Available(feature) &&
        (publishing ? current->Enabled(feature) : variables[index].value == 1);
}
int CSOL_ReadZombieGibChance(bool meleeHeadshot)
{
    if (!CSOL_CustomFeatureEnabled(custom::Feature::ZombieGibs)) return 0;
    return static_cast<int>(LiveNumber(meleeHeadshot ? custom::Setting::ZombieGibMeleeHeadChance :
        custom::Setting::ZombieGibChance).value_or(0));
}
std::optional<int> CSOL_ReadAmmoRefillPrice()
{
    if (!CSOL_CustomFeatureEnabled(custom::Feature::AmmoRefill)) return {};
    const auto price = LiveNumber(custom::Setting::AmmoRefillPrice);
    return price ? std::optional<int>(static_cast<int>(*price)) : std::nullopt;
}

int CSOL_ReadNightEquipmentPrice(bool lantern)
{
    const int fallback = lantern ? guest::cof::light::Lantern.price : guest::cof::light::Flare.price;
    if (!registered || !current) return fallback;
    const auto key = lantern ? custom::Setting::NightLanternPrice : custom::Setting::NightFlarePrice;
    return static_cast<int>(LiveNumber(key).value_or(fallback));
}

std::optional<std::array<int, custom::BlindBoxCount>> CSOL_ReadBlindBoxPrices()
{
    if (!CSOL_CustomFeatureEnabled(custom::Feature::BlindBox)) return {};
    std::array<int, custom::BlindBoxCount> prices{};
    int previous = -1;
    for (const auto& box : custom::BlindBoxes())
    {
        const auto index = static_cast<std::size_t>(box.price);
        const auto& setting = custom::Settings()[index];
        const double value = publishing ? current->Number(box.price).value_or(-1) :
            variables[custom::FeatureCount + index].value;
        if (!(value >= setting.minimum && value <= setting.maximum) || std::trunc(value) != value || value <= previous)
            return {};
        previous = static_cast<int>(value);
        prices[static_cast<std::size_t>(box.id)] = previous;
    }
    return prices;
}
bool CSOL_ReadTimeCycleControls(custom::TimeCycleControls& output)
{
    if (!registered || !current) return false;
    const auto number = [&](custom::Setting setting)
    {
        return publishing ? current->Number(setting).value_or(-1) :
            double(variables[custom::FeatureCount + std::size_t(setting)].value);
    };
    const double noon = number(custom::Setting::MiddaySeconds), dusk = number(custom::Setting::DuskSeconds);
    const float enabled = publishing ? (current->Enabled(custom::Feature::TimeCycle) ? 1 : 0) :
        variables[std::size_t(custom::Feature::TimeCycle)].value;
    if ((enabled != 0 && enabled != 1) || !std::isfinite(noon) || !std::isfinite(dusk) ||
        noon < 1 || noon > 86400 || dusk < 1 || dusk > 86400 || std::trunc(noon) != noon || std::trunc(dusk) != dusk)
        return false;
    output = {enabled == 1, {float(noon), float(dusk)}};
    return true;
}
custom::MonsterPhaseBuff CSOL_ReadMonsterPhaseBuff(custom::Phase phase)
{
    const bool night = phase == custom::Phase::Night;
    if ((!night && phase != custom::Phase::Dusk) ||
        !CSOL_CustomFeatureEnabled(night ? custom::Feature::NightBuff : custom::Feature::DuskBuff))
        return {};

    const auto number = [](custom::Setting id, float neutral)
    { return static_cast<float>(LiveNumber(id).value_or(neutral)); };
    return {number(night ? custom::Setting::NightSpeedMultiplier : custom::Setting::DuskSpeedMultiplier, 1),
        night ? number(custom::Setting::NightDamageMultiplier, 1) : 1.0f,
        number(night ? custom::Setting::NightRegenPerSecond : custom::Setting::DuskRegenPerSecond, 0)};
}
std::optional<custom::MonsterPopulationControls> CSOL_ReadMonsterPopulation(custom::Phase phase)
{
    if (phase == custom::Phase::Ended || !CSOL_CustomFeatureEnabled(custom::Feature::ZombiePopulation))
        return {};
    using Setting = custom::Setting;
    const auto base = LiveNumber(phase == custom::Phase::Noon ? Setting::MiddayZombieBase :
        phase == custom::Phase::Dusk ? Setting::DuskZombieBase : Setting::NightZombieBase);
    const auto perPlayer = LiveNumber(Setting::ZombiesPerPlayer);
    if (!base || !perPlayer) return {};

    custom::MonsterPopulationControls result{static_cast<int>(*base), static_cast<int>(*perPlayer), {}};
    if (phase == custom::Phase::Night && CSOL_CustomFeatureEnabled(custom::Feature::RandomHorde))
    {
        const auto interval = LiveNumber(Setting::HordeIntervalSeconds);
        const auto probability = LiveNumber(Setting::HordeProbability);
        const auto duration = LiveNumber(Setting::HordeDurationSeconds);
        const auto extra = LiveNumber(Setting::HordeExtraZombies);
        if (interval && probability && duration && extra)
            result.horde = custom::HordeControls{*interval, *probability, *duration, static_cast<int>(*extra)};
    }
    return result;
}
bool CSOL_ReadNightLightingControls(custom::NightLightingConfig& output)
{
    if (!registered || !current) return false;
    const auto number = [&](custom::Setting setting)
    {
        return publishing ? current->Number(setting).value_or(-1) :
            double(variables[custom::FeatureCount + std::size_t(setting)].value);
    };
    const float enabled = publishing ? (current->Enabled(custom::Feature::NightPlayerLights) ? 1 : 0) :
        variables[std::size_t(custom::Feature::NightPlayerLights)].value;
    if (enabled != 0 && enabled != 1) return false;
    const double radius = number(custom::Setting::NightPlayerLightRadius);
    const double brightness = number(custom::Setting::NightPlayerLightBrightness);
    const double color = number(custom::Setting::NightPlayerLightColor);
    if (!std::isfinite(radius) || radius < 16 || radius > 512 ||
        !std::isfinite(brightness) || brightness < 0 || brightness > 255 || std::trunc(brightness) != brightness ||
        !std::isfinite(color) || color < 0 || color > 0xffffff || std::trunc(color) != color)
    {
        // Invalid numeric overrides retain the last valid policy. Disabling
        // remains available without first repairing an unrelated number.
        if (enabled) return false;
        output.enabled = false; return true;
    }
    output = {enabled == 1, float(radius), std::uint32_t(color), std::uint8_t(brightness)};
    return true;
}
