// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace csol::scenario::custom
{
// One bounded document includes comments for every feature and setting. Keep
// the documented 512-byte comments serializable even with maximum escaping.
inline constexpr std::size_t MaximumBytes = 96 * 1024;
enum class Feature : std::uint8_t
{ Kick, TimeCycle, BlindBox, Mastery, CofSprint, Blood, DuskBuff, NightBuff, ZombiePopulation, RandomHorde, DamageRefundCap, NightPlayerLights, Count };
enum class Setting : std::uint8_t
{
    MiddaySeconds, DuskSeconds, BlindBoxPrice1, BlindBoxPrice2, BlindBoxPrice3,
    MasteryThreshold1, MasteryThreshold2, MasteryThreshold3,
    MasteryReload1, MasteryReload2, MasteryDamage2, MasteryDamage3, MasteryFireRate3,
    SprintMaximum, SprintDrainPerSecond, SprintRecoveryPerSecond, SprintSpeed,
    WalkSpeed, JumpCost, KickCost, DuskMaximumMultiplier, DuskRecoveryMultiplier,
    BloodLifetimeSeconds, BloodMaximum,
    DuskSpeedMultiplier, DuskRegenPerSecond,
    NightDamageMultiplier, NightSpeedMultiplier, NightRegenPerSecond,
    MiddayZombieBase, DuskZombieBase, NightZombieBase, ZombiesPerPlayer,
    HordeIntervalSeconds, HordeProbability, HordeDurationSeconds,
    DamageRefundMaximumPerMonster,
    DuskLightLevel, NightLightLevel, LightStepSeconds, NightFogDensity,
    NightFogColor, NightFogStart, NightFogEnd, NightFogSkybox,
    SprintCostPerUnit, NightPlayerLightRadius, NightPlayerLightBrightness, NightPlayerLightColor,
    HordeExtraZombies, Count
};
enum class ValueType : std::uint8_t { Boolean, Integer, Real };
enum class Scope : std::uint8_t { Immediate, NextMapSession };
inline constexpr std::size_t FeatureCount = static_cast<std::size_t>(Feature::Count);
inline constexpr std::size_t SettingCount = static_cast<std::size_t>(Setting::Count);
enum class BlindBox : std::uint8_t { Cof, CounterStrike, Csol, Count };
inline constexpr std::size_t BlindBoxCount = static_cast<std::size_t>(BlindBox::Count);
inline constexpr std::size_t MaximumBlindBoxEntries = 64;
inline constexpr std::uint32_t MaximumBlindBoxWeight = 1000000;
struct BlindBoxEntry { std::string weapon; std::uint32_t weight{}; };
struct BlindBoxDescriptor { BlindBox id; Setting price; std::string_view key, comment; };
std::span<const BlindBoxDescriptor, BlindBoxCount> BlindBoxes() noexcept;
std::optional<BlindBox> FindBlindBox(std::string_view key) noexcept;

struct FeatureDescriptor
{
    Feature id;
    std::string_view key, cvar, comment;
    bool defaultValue;
    Scope scope;
};
struct SettingDescriptor
{
    Setting id;
    Feature feature;
    std::string_view key, cvar, comment;
    ValueType type;
    double minimum, maximum;
    std::optional<double> defaultValue;
    Scope scope;
};
std::span<const FeatureDescriptor, FeatureCount> Features() noexcept;
std::span<const SettingDescriptor, SettingCount> Settings() noexcept;
std::optional<Feature> FindFeature(std::string_view key) noexcept;
std::optional<Setting> FindSetting(std::string_view key) noexcept;

struct Error { std::size_t byteOffset{}; std::string field, message; };

// Published only as shared_ptr<const Snapshot>. Fixed arrays make all hot
// getters allocation-free; strings belong to cold parse/edit/serialization.
class Snapshot
{
public:
    bool Enabled(Feature feature) const noexcept;
    std::optional<double> Number(Setting setting) const noexcept;
    std::optional<std::int32_t> Integer(Setting setting) const noexcept;
    std::string_view Comment(Feature feature) const noexcept;
    std::string_view Comment(Setting setting) const noexcept;
    std::span<const BlindBoxEntry> Pool(BlindBox box) const noexcept;
    std::string_view Comment(BlindBox box) const noexcept;
private:
    friend class Draft;
    std::array<bool, FeatureCount> features_{};
    std::array<std::optional<double>, SettingCount> values_{};
    std::array<std::string, FeatureCount> featureComments_;
    std::array<std::string, SettingCount> valueComments_;
    std::array<std::vector<BlindBoxEntry>, BlindBoxCount> pools_;
    std::array<std::string, BlindBoxCount> poolComments_;
};
using SnapshotPtr = std::shared_ptr<const Snapshot>;

// Cold transaction; modifying a Draft never alters a published snapshot.
// Finish checks enabled-feature requirements and cross-field relationships.
class Draft
{
public:
    Draft();
    explicit Draft(const Snapshot& snapshot);
    bool Set(Feature feature, bool enabled, Error& error);
    // nullopt resets a known default, or removes an optional value that has
    // no default. This matches omission on the next JSON load.
    bool Set(Setting setting, std::optional<double> value, Error& error);
    bool SetComment(Feature feature, std::string_view value, Error& error);
    bool SetComment(Setting setting, std::string_view value, Error& error);
    bool SetPool(BlindBox box, std::span<const BlindBoxEntry> entries, Error& error);
    bool SetComment(BlindBox box, std::string_view value, Error& error);
    bool Finish(SnapshotPtr& output, Error& error) const;
private:
    Snapshot value_;
};
// Canonical version2 is one grouped document:
// {version:2, features:{feature:{enabled:bool,comment:string,
// values:{setting:{value:number|null,comment:string}}}}}.
// Settings must belong to their descriptor's feature. Null is permitted only
// for absent optional settings without a default. Omitted feature/setting
// objects retain defaults; present objects require every declared field.
// Published version1's exact four-root features/values/comments envelope is
// still accepted. Serialize always emits version2 with prose adjacent to the
// setting it explains, including null optional entries so prose is preserved.
// Parse/Finish preserve output on rejection. JSON // and block comments parse.
// blind_box optionally contains pools:{cof|cs16|csol:{comment, weapons:[
// {weapon:catalog_alias,weight:integer0..1000000}]}}. Missing pools retain
// authored defaults; an explicit empty pool disables its draws. At most64
// unique aliases per pool. Aliases resolve against the actual server catalog.
bool Parse(std::string_view json, SnapshotPtr& output, Error& error);
std::string Serialize(const Snapshot& snapshot);
}
