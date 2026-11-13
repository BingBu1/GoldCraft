// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "gameplay_settings.h"
#include "settings_store.hpp"
#include "../game_mode.h"
#include "../scenario/core/human_kick.h"
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
    // Consumers register their capability as they are integrated. Publishing
    // a configuration flag alone cannot claim an unimplemented gameplay path.
    return feature == custom::Feature::Kick;
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
