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

namespace csol::scenario::custom
{
inline constexpr std::size_t MaximumBytes = 64 * 1024;
enum class Feature : std::uint8_t
{ Kick, TimeCycle, BlindBox, Mastery, CofSprint, Blood, DuskBuff, NightBuff, ZombiePopulation, RandomHorde, DamageRefundCap, Count };
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
    DamageRefundMaximumPerMonster, Count
};
enum class ValueType : std::uint8_t { Boolean, Integer, Real };
enum class Scope : std::uint8_t { Immediate, NextMapSession };
inline constexpr std::size_t FeatureCount = static_cast<std::size_t>(Feature::Count);
inline constexpr std::size_t SettingCount = static_cast<std::size_t>(Setting::Count);

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
private:
    friend class Draft;
    std::array<bool, FeatureCount> features_{};
    std::array<std::optional<double>, SettingCount> values_{};
    std::array<std::string, FeatureCount> featureComments_;
    std::array<std::string, SettingCount> valueComments_;
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
    bool Finish(SnapshotPtr& output, Error& error) const;
private:
    Snapshot value_;
};
// Exactly {version:1, features:{known bools}, values:{known numbers},
// comments:{features:{known strings}, values:{known strings}}}. Fields may
// be omitted inside features/values to retain documented defaults; all four
// root fields and both comments containers are mandatory. Missing
// optional values are omitted from serialization, never emitted as null.
// Both parse and Finish leave output unchanged on rejection. JSON // and
// block comments are accepted; persistent prose lives in comments fields.
bool Parse(std::string_view json, SnapshotPtr& output, Error& error);
std::string Serialize(const Snapshot& snapshot);
}
