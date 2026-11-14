// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cmath>

namespace csol::scenario::custom
{
namespace
{
constexpr std::array<FeatureDescriptor, FeatureCount> FeatureRows{{
    {Feature::Kick, "kick", "csol_custom_kick", "F范围踢击开关；关闭不删除配置，可再次开启。", true, Scope::Immediate},
    {Feature::TimeCycle, "time_cycle", "csol_custom_time_cycle", "正午与黄昏按秒推进，之后保持永夜；BGM资源由服务端另验。", false, Scope::NextMapSession},
    {Feature::BlindBox, "blind_box", "csol_custom_blind_box", "三档盲盒使用本局CS金钱；奖池和真实发奖能力另验。", false, Scope::Immediate},
    {Feature::Mastery, "mastery", "csol_custom_mastery", "本图熟练度100/200/300；一档换弹，二档换弹和伤害，三档射速和伤害。", false, Scope::NextMapSession},
    {Feature::CofSprint, "cof_sprint", "csol_custom_cof_sprint", "CoF体力跑；原消耗恢复及动作消费者先取证，正午无限不能用NaN表示。", false, Scope::NextMapSession},
    {Feature::Blood, "blood", "csol_custom_blood", "血迹效果；最大数量与寿命有界，关闭只清理本功能拥有的实例。", false, Scope::Immediate},
    {Feature::DuskBuff, "dusk_buff", "csol_custom_dusk_buff", "黄昏独立移速和回血参数；关闭保留参数。", false, Scope::NextMapSession},
    {Feature::NightBuff, "night_buff", "csol_custom_night_buff", "永夜独立伤害、移速和回血参数；不开启不要求未定参数。", false, Scope::NextMapSession},
    {Feature::ZombiePopulation, "zombie_population", "csol_custom_zombie_population", "分阶段基础怪量与每玩家增加量；实际池容量由消费者核验。", false, Scope::NextMapSession},
    {Feature::RandomHorde, "random_horde", "csol_custom_random_horde", "随机尸潮；间隔、0到1概率和持续时间必须明确。", false, Scope::NextMapSession},
    {Feature::DamageRefundCap, "damage_refund_cap", "csol_custom_damage_refund_cap", "返现上限对每位玩家与每只怪物的组合分别累计，新怪租约获得新额度。", false, Scope::NextMapSession}
}};

// Bounds are host configuration/overflow limits, not recovered CSOL values.
// Phase times/thresholds are user-specified. Other defaults below are explicit
// GoldCraft custom balance decisions, not recovered CSOL values. Unknown CoF
// base parameters remain absent instead of silently defining partial behavior.
constexpr std::array<SettingDescriptor, SettingCount> SettingRows{{
    {Setting::MiddaySeconds, Feature::TimeCycle, "midday_seconds", "csol_custom_midday_seconds", "正午持续秒数；之后进入黄昏。", ValueType::Integer, 1, 86400, 180, Scope::NextMapSession},
    {Setting::DuskSeconds, Feature::TimeCycle, "dusk_seconds", "csol_custom_dusk_seconds", "黄昏持续秒数；之后永久保持夜间。", ValueType::Integer, 1, 86400, 120, Scope::NextMapSession},
    {Setting::BlindBoxPrice1, Feature::BlindBox, "blind_box_price_1", "csol_custom_blind_box_price_1", "第一档CS金钱价格，三档严格递增。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 3000, Scope::Immediate},
    {Setting::BlindBoxPrice2, Feature::BlindBox, "blind_box_price_2", "csol_custom_blind_box_price_2", "第二档CS金钱价格，不用仓库币。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 6000, Scope::Immediate},
    {Setting::BlindBoxPrice3, Feature::BlindBox, "blind_box_price_3", "csol_custom_blind_box_price_3", "第三档CS金钱价格，真实发奖另验。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 9000, Scope::Immediate},
    {Setting::MasteryThreshold1, Feature::Mastery, "mastery_threshold_1", "csol_custom_mastery_threshold_1", "本图累计熟练度第一档阈值。", ValueType::Integer, 1, 1000000000, 100, Scope::NextMapSession},
    {Setting::MasteryThreshold2, Feature::Mastery, "mastery_threshold_2", "csol_custom_mastery_threshold_2", "本图累计熟练度第二档阈值，严格大于第一档。", ValueType::Integer, 1, 1000000000, 200, Scope::NextMapSession},
    {Setting::MasteryThreshold3, Feature::Mastery, "mastery_threshold_3", "csol_custom_mastery_threshold_3", "本图累计熟练度第三档阈值，严格大于第二档。", ValueType::Integer, 1, 1000000000, 300, Scope::NextMapSession},
    {Setting::MasteryReload1, Feature::Mastery, "mastery_reload_1", "csol_custom_mastery_reload_1", "一档换弹时长倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, .85, Scope::NextMapSession},
    {Setting::MasteryReload2, Feature::Mastery, "mastery_reload_2", "csol_custom_mastery_reload_2", "二档换弹时长倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, .7, Scope::NextMapSession},
    {Setting::MasteryDamage2, Feature::Mastery, "mastery_damage_2", "csol_custom_mastery_damage_2", "二档伤害倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.10, Scope::NextMapSession},
    {Setting::MasteryDamage3, Feature::Mastery, "mastery_damage_3", "csol_custom_mastery_damage_3", "三档伤害倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.20, Scope::NextMapSession},
    {Setting::MasteryFireRate3, Feature::Mastery, "mastery_fire_rate_3", "csol_custom_mastery_fire_rate_3", "三档每秒射击频率倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.10, Scope::NextMapSession},
    {Setting::SprintMaximum, Feature::CofSprint, "sprint_maximum", "csol_custom_sprint_maximum", "有限阶段体力上限，正午无限由阶段规则表达。", ValueType::Real, .001, 1000000, {}, Scope::NextMapSession},
    {Setting::SprintDrainPerSecond, Feature::CofSprint, "sprint_drain_per_second", "csol_custom_sprint_drain_per_second", "每秒跑动体力消耗，必须显式填写。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::SprintRecoveryPerSecond, Feature::CofSprint, "sprint_recovery_per_second", "csol_custom_sprint_recovery_per_second", "每秒体力恢复，必须显式填写。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::SprintSpeed, Feature::CofSprint, "sprint_speed", "csol_custom_sprint_speed", "跑动基础速度，仍须宿主碰撞和移动权威验证。", ValueType::Real, .001, 4096, {}, Scope::NextMapSession},
    {Setting::WalkSpeed, Feature::CofSprint, "walk_speed", "csol_custom_walk_speed", "普通行走基础速度，不覆盖其他功能拥有的速度限制。", ValueType::Real, .001, 4096, {}, Scope::NextMapSession},
    {Setting::JumpCost, Feature::CofSprint, "jump_cost", "csol_custom_jump_cost", "跳跃单次体力消耗。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::KickCost, Feature::CofSprint, "kick_cost", "csol_custom_kick_cost", "踢击单次体力消耗，接受动作后由权威消费者记账。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::DuskMaximumMultiplier, Feature::CofSprint, "dusk_maximum_multiplier", "csol_custom_dusk_maximum_multiplier", "黄昏体力上限倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.5, Scope::NextMapSession},
    {Setting::DuskRecoveryMultiplier, Feature::CofSprint, "dusk_recovery_multiplier", "csol_custom_dusk_recovery_multiplier", "黄昏体力恢复倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.25, Scope::NextMapSession},
    {Setting::BloodLifetimeSeconds, Feature::Blood, "blood_lifetime_seconds", "csol_custom_blood_lifetime_seconds", "本功能血迹寿命秒数，上限600秒避免无界留存。", ValueType::Real, .001, 600, {}, Scope::Immediate},
    {Setting::BloodMaximum, Feature::Blood, "blood_maximum", "csol_custom_blood_maximum", "本功能同时存活血迹硬上限，最多4096。", ValueType::Integer, 1, 4096, {}, Scope::Immediate},
    {Setting::DuskSpeedMultiplier, Feature::DuskBuff, "dusk_speed_multiplier", "csol_custom_dusk_speed_multiplier", "黄昏基础移速倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.05, Scope::NextMapSession},
    {Setting::DuskRegenPerSecond, Feature::DuskBuff, "dusk_regen_per_second", "csol_custom_dusk_regen_per_second", "黄昏每秒回血，0为无额外回血。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000000, 0, Scope::NextMapSession},
    {Setting::NightDamageMultiplier, Feature::NightBuff, "night_damage_multiplier", "csol_custom_night_damage_multiplier", "永夜基础伤害倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.20, Scope::NextMapSession},
    {Setting::NightSpeedMultiplier, Feature::NightBuff, "night_speed_multiplier", "csol_custom_night_speed_multiplier", "永夜基础移速倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.10, Scope::NextMapSession},
    {Setting::NightRegenPerSecond, Feature::NightBuff, "night_regen_per_second", "csol_custom_night_regen_per_second", "永夜每秒回血，0为无额外回血。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000000, 0, Scope::NextMapSession},
    {Setting::MiddayZombieBase, Feature::ZombiePopulation, "midday_zombie_base", "csol_custom_midday_zombie_base", "正午基础怪物目标数量；实际池容量仍需验证。", ValueType::Integer, 0, 4096, {}, Scope::NextMapSession},
    {Setting::DuskZombieBase, Feature::ZombiePopulation, "dusk_zombie_base", "csol_custom_dusk_zombie_base", "黄昏基础怪物目标数量。", ValueType::Integer, 0, 4096, {}, Scope::NextMapSession},
    {Setting::NightZombieBase, Feature::ZombiePopulation, "night_zombie_base", "csol_custom_night_zombie_base", "永夜基础怪物目标数量。", ValueType::Integer, 0, 4096, {}, Scope::NextMapSession},
    {Setting::ZombiesPerPlayer, Feature::ZombiePopulation, "zombies_per_player", "csol_custom_zombies_per_player", "每位玩家额外目标怪数；加乘须检查实际池上限。", ValueType::Integer, 0, 4096, {}, Scope::NextMapSession},
    {Setting::HordeIntervalSeconds, Feature::RandomHorde, "horde_interval_seconds", "csol_custom_horde_interval_seconds", "随机尸潮抽样间隔秒数，必须为正。", ValueType::Real, .001, 86400, {}, Scope::NextMapSession},
    {Setting::HordeProbability, Feature::RandomHorde, "horde_probability", "csol_custom_horde_probability", "每次抽样尸潮概率，0到1而非百分整数。", ValueType::Real, 0, 1, {}, Scope::NextMapSession},
    {Setting::HordeDurationSeconds, Feature::RandomHorde, "horde_duration_seconds", "csol_custom_horde_duration_seconds", "单次尸潮持续秒数，必须为正。", ValueType::Real, .001, 86400, {}, Scope::NextMapSession},
    {Setting::DamageRefundMaximumPerMonster, Feature::DamageRefundCap, "damage_refund_max_per_monster", "csol_custom_damage_refund_max_per_monster", "每player-victim组合累计CS金钱上限，怪物新租约是新额度。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 1000, Scope::NextMapSession},
    {Setting::DuskLightLevel, Feature::TimeCycle, "dusk_light_level", "csol_custom_dusk_light_level", "黄昏光照字符的ASCII值，97最暗到122最亮；默认105为原版i档。", ValueType::Integer, 97, 122, 105, Scope::NextMapSession},
    {Setting::NightLightLevel, Feature::TimeCycle, "night_light_level", "csol_custom_night_light_level", "永夜最终光照档，不得高于黄昏；按用户要求默认98为更暗b档，99保留原版c档。", ValueType::Integer, 97, 122, 98, Scope::NextMapSession},
    {Setting::LightStepSeconds, Feature::TimeCycle, "light_step_seconds", "csol_custom_light_step_seconds", "永夜逐次降低一档的间隔秒数；默认原版2.5，超时不追赶多档。", ValueType::Real, .001, 3600, 2.5, Scope::NextMapSession},
    {Setting::NightFogDensity, Feature::TimeCycle, "night_fog_density", "csol_custom_night_fog_density", "永夜最暗档的雾密度；0关闭。默认原版float位型3A378034。", ValueType::Real, 0, .01, 0.000699999975040555, Scope::NextMapSession},
    {Setting::NightFogColor, Feature::TimeCycle, "night_fog_color", "csol_custom_night_fog_color", "雾颜色的十进制RGB打包值0xRRGGBB；默认65793对应原版(1,1,1)。", ValueType::Integer, 0, 16777215, 65793, Scope::NextMapSession},
    {Setting::NightFogStart, Feature::TimeCycle, "night_fog_start", "csol_custom_night_fog_start", "标准Fog接口起始距离，默认原版100，须小于结束距离。", ValueType::Real, 0, 999999, 100, Scope::NextMapSession},
    {Setting::NightFogEnd, Feature::TimeCycle, "night_fog_end", "csol_custom_night_fog_end", "标准Fog接口结束距离，默认原版2000，须大于起始距离。", ValueType::Real, .001, 1000000, 2000, Scope::NextMapSession},
    {Setting::NightFogSkybox, Feature::TimeCycle, "night_fog_skybox", "csol_custom_night_fog_skybox", "雾是否影响天空盒：0否，1是；默认原版1。", ValueType::Integer, 0, 1, 1, Scope::NextMapSession}
}};

constexpr bool DescriptorOrder()
{
    for (std::size_t i = 0; i < FeatureRows.size(); ++i)
        if (static_cast<std::size_t>(FeatureRows[i].id) != i) return false;
    for (std::size_t i = 0; i < SettingRows.size(); ++i)
        if (static_cast<std::size_t>(SettingRows[i].id) != i) return false;
    return true;
}
static_assert(DescriptorOrder());

bool Fail(Error& error, std::string_view field, const char* message)
{ error = {0, std::string(field), message}; return false; }
bool TextValid(std::string_view text)
{
    if (text.size() > 512 || text.find('\0') != text.npos) return false;
    rapidjson::MemoryStream stream(text.data(), text.size());
    while (stream.Tell() < text.size())
    {
        unsigned codepoint{};
        if (!rapidjson::UTF8<>::Decode(stream, &codepoint) ||
            (codepoint < 32 && codepoint != '\t' && codepoint != '\r' && codepoint != '\n')) return false;
    }
    return true;
}
bool NumberValid(const SettingDescriptor& descriptor, double value)
{
    return std::isfinite(value) && value >= descriptor.minimum && value <= descriptor.maximum &&
        (descriptor.type != ValueType::Integer || std::trunc(value) == value);
}
template<std::size_t N>
bool Fields(const rapidjson::Value& object, const std::array<std::string_view, N>& allowed,
    std::string_view field, Error& error)
{
    if (!object.IsObject()) return Fail(error, field, "expected an object");
    std::array<bool, N> seen{};
    for (const auto& member : object.GetObject())
    {
        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
        const auto found = std::find(allowed.begin(), allowed.end(), key);
        if (found == allowed.end()) return Fail(error, field, "unknown field");
        const auto index = static_cast<std::size_t>(found - allowed.begin());
        if (seen[index]) return Fail(error, field, "duplicate field");
        seen[index] = true;
    }
    if (object.MemberCount() != N) return Fail(error, field, "required field is missing");
    return true;
}
template<bool FeatureKeys>
bool ReadEntries(const rapidjson::Value& object, Draft& draft, bool comments, Error& error)
{
    if (!object.IsObject()) return Fail(error, comments ? "comments" : (FeatureKeys ? "features" : "values"), "expected an object");
    constexpr auto count = FeatureKeys ? FeatureCount : SettingCount;
    std::array<bool, count> seen{};
    for (const auto& member : object.GetObject())
    {
        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
        const auto id = [&]
        {
            if constexpr (FeatureKeys) return FindFeature(key);
            else return FindSetting(key);
        }();
        if (!id) return Fail(error, key, "unknown setting identity");
        const auto index = static_cast<std::size_t>(*id);
        if (seen[index]) return Fail(error, key, "duplicate setting identity");
        seen[index] = true;
        if (comments)
        {
            if (!member.value.IsString()) return Fail(error, key, "explanation must be a string");
            if (!draft.SetComment(*id, {member.value.GetString(), member.value.GetStringLength()}, error)) return false;
        }
        else if constexpr (FeatureKeys)
        {
            if (!member.value.IsBool()) return Fail(error, key, "feature must be a JSON boolean");
            if (!draft.Set(*id, member.value.GetBool(), error)) return false;
        }
        else
        {
            if (!member.value.IsNumber() || (SettingRows[index].type == ValueType::Integer && !member.value.IsInt64() && !member.value.IsUint64()))
                return Fail(error, key, "setting requires its declared numeric type");
            if (!draft.Set(*id, member.value.GetDouble(), error)) return false;
        }
    }
    return true;
}
bool ReadGrouped(const rapidjson::Value& object, Draft& draft, Error& error)
{
    if (!object.IsObject()) return Fail(error, "features", "expected an object");
    std::array<bool, FeatureCount> seen{};
    for (const auto& member : object.GetObject())
    {
        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
        const auto feature = FindFeature(key);
        if (!feature) return Fail(error, key, "unknown feature identity");
        const auto index = static_cast<std::size_t>(*feature);
        if (seen[index]) return Fail(error, key, "duplicate feature identity");
        seen[index] = true;
        const auto& group = member.value;
        if (!Fields(group, std::array<std::string_view, 3>{"enabled", "comment", "values"}, key, error)) return false;
        if (!group["enabled"].IsBool()) return Fail(error, key, "enabled must be a JSON boolean");
        if (!group["comment"].IsString()) return Fail(error, key, "explanation must be a string");
        if (!draft.Set(*feature, group["enabled"].GetBool(), error) ||
            !draft.SetComment(*feature, {group["comment"].GetString(), group["comment"].GetStringLength()}, error)) return false;
        const auto& values = group["values"];
        if (!values.IsObject()) return Fail(error, key, "values must be an object");
        std::array<bool, SettingCount> valueSeen{};
        for (const auto& entry : values.GetObject())
        {
            const std::string_view name(entry.name.GetString(), entry.name.GetStringLength());
            const auto setting = FindSetting(name);
            if (!setting) return Fail(error, name, "unknown setting identity");
            const auto settingIndex = static_cast<std::size_t>(*setting);
            const auto& descriptor = SettingRows[settingIndex];
            if (descriptor.feature != *feature) return Fail(error, name, "setting belongs to a different feature");
            if (valueSeen[settingIndex]) return Fail(error, name, "duplicate setting identity");
            valueSeen[settingIndex] = true;
            const auto& leaf = entry.value;
            if (!Fields(leaf, std::array<std::string_view, 2>{"value", "comment"}, name, error)) return false;
            if (!leaf["comment"].IsString()) return Fail(error, name, "explanation must be a string");
            const auto& number = leaf["value"];
            if (number.IsNull())
            {
                if (descriptor.defaultValue) return Fail(error, name, "null is only valid for an optional setting without a default");
                if (!draft.Set(*setting, std::nullopt, error)) return false;
            }
            else
            {
                if (!number.IsNumber() || (descriptor.type == ValueType::Integer && !number.IsInt64() && !number.IsUint64()))
                    return Fail(error, name, "setting requires its declared numeric type");
                if (!draft.Set(*setting, number.GetDouble(), error)) return false;
            }
            if (!draft.SetComment(*setting, {leaf["comment"].GetString(), leaf["comment"].GetStringLength()}, error)) return false;
        }
    }
    return true;
}
}

std::span<const FeatureDescriptor, FeatureCount> Features() noexcept { return FeatureRows; }
std::span<const SettingDescriptor, SettingCount> Settings() noexcept { return SettingRows; }
std::optional<Feature> FindFeature(std::string_view key) noexcept
{
    for (const auto& descriptor : FeatureRows) if (descriptor.key == key) return descriptor.id;
    return {};
}
std::optional<Setting> FindSetting(std::string_view key) noexcept
{
    for (const auto& descriptor : SettingRows) if (descriptor.key == key) return descriptor.id;
    return {};
}
bool Snapshot::Enabled(Feature feature) const noexcept
{ const auto index = static_cast<std::size_t>(feature); return index < features_.size() && features_[index]; }
std::optional<double> Snapshot::Number(Setting setting) const noexcept
{ const auto index = static_cast<std::size_t>(setting); return index < values_.size() ? values_[index] : std::nullopt; }
std::optional<std::int32_t> Snapshot::Integer(Setting setting) const noexcept
{
    const auto index = static_cast<std::size_t>(setting);
    return index < values_.size() && SettingRows[index].type == ValueType::Integer && values_[index]
        ? std::optional{static_cast<std::int32_t>(*values_[index])} : std::nullopt;
}
std::string_view Snapshot::Comment(Feature feature) const noexcept
{ const auto index = static_cast<std::size_t>(feature); return index < featureComments_.size() ? featureComments_[index] : std::string_view{}; }
std::string_view Snapshot::Comment(Setting setting) const noexcept
{ const auto index = static_cast<std::size_t>(setting); return index < valueComments_.size() ? valueComments_[index] : std::string_view{}; }

Draft::Draft()
{
    for (const auto& descriptor : FeatureRows)
    {
        const auto index = static_cast<std::size_t>(descriptor.id);
        value_.features_[index] = descriptor.defaultValue;
        value_.featureComments_[index] = descriptor.comment;
    }
    for (const auto& descriptor : SettingRows)
    {
        const auto index = static_cast<std::size_t>(descriptor.id);
        value_.values_[index] = descriptor.defaultValue;
        value_.valueComments_[index] = descriptor.comment;
    }
}
Draft::Draft(const Snapshot& snapshot) : value_(snapshot) {}
bool Draft::Set(Feature feature, bool enabled, Error& error)
{
    const auto index = static_cast<std::size_t>(feature);
    if (index >= FeatureCount) return Fail(error, "features", "invalid feature identity");
    value_.features_[index] = enabled; error = {}; return true;
}
bool Draft::Set(Setting setting, std::optional<double> number, Error& error)
{
    const auto index = static_cast<std::size_t>(setting);
    if (index >= SettingCount) return Fail(error, "values", "invalid numeric setting identity");
    if (number && !NumberValid(SettingRows[index], *number))
        return Fail(error, SettingRows[index].key, "numeric value is nonfinite, out of range or nonintegral");
    value_.values_[index] = number ? number : SettingRows[index].defaultValue;
    error = {}; return true;
}
bool Draft::SetComment(Feature feature, std::string_view text, Error& error)
{
    const auto index = static_cast<std::size_t>(feature);
    if (index >= FeatureCount) return Fail(error, "comments.features", "invalid feature identity");
    if (!TextValid(text)) return Fail(error, FeatureRows[index].key, "explanation must be valid UTF8, at most512 bytes, without NUL/control bytes");
    value_.featureComments_[index] = text; error = {}; return true;
}
bool Draft::SetComment(Setting setting, std::string_view text, Error& error)
{
    const auto index = static_cast<std::size_t>(setting);
    if (index >= SettingCount) return Fail(error, "comments.values", "invalid numeric setting identity");
    if (!TextValid(text)) return Fail(error, SettingRows[index].key, "explanation must be valid UTF8, at most512 bytes, without NUL/control bytes");
    value_.valueComments_[index] = text; error = {}; return true;
}
bool Draft::Finish(SnapshotPtr& output, Error& error) const
{
    for (const auto& descriptor : SettingRows)
        if (value_.Enabled(descriptor.feature) && !value_.Number(descriptor.id))
            return Fail(error, descriptor.key, "enabled feature requires an explicit numeric parameter");
    const auto increasing = [&](Setting first, Setting second, Setting third)
    {
        const auto a = value_.Number(first), b = value_.Number(second), c = value_.Number(third);
        return (!a || !b || *a < *b) && (!b || !c || *b < *c) && (!a || !c || *a < *c);
    };
    if (!increasing(Setting::BlindBoxPrice1, Setting::BlindBoxPrice2, Setting::BlindBoxPrice3))
        return Fail(error, "blind_box_price_1..3", "blind box prices must be strictly increasing");
    if (!increasing(Setting::MasteryThreshold1, Setting::MasteryThreshold2, Setting::MasteryThreshold3))
        return Fail(error, "mastery_threshold_1..3", "mastery thresholds must be strictly increasing");
    if (value_.Number(Setting::NightLightLevel) > value_.Number(Setting::DuskLightLevel))
        return Fail(error, "night_light_level", "night light level must not exceed dusk level");
    if (value_.Number(Setting::NightFogStart) >= value_.Number(Setting::NightFogEnd))
        return Fail(error, "night_fog_start", "fog start must be below fog end");
    output = std::make_shared<const Snapshot>(value_); error = {}; return true;
}
bool Parse(std::string_view json, SnapshotPtr& output, Error& error)
{
    if (json.empty() || json.size() > MaximumBytes || json.find('\0') != json.npos)
        return Fail(error, "root", "configuration must contain1..65536 UTF8 bytes without NUL");
    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseCommentsFlag |
        rapidjson::kParseIterativeFlag | rapidjson::kParseFullPrecisionFlag>(json.data(), json.size());
    if (document.HasParseError())
    { error = {document.GetErrorOffset(), "root", "invalid JSON or UTF8 encoding"}; return false; }
    if (!document.IsObject()) return Fail(error, "root", "expected an object");
    const auto version = document.FindMember("version");
    if (version == document.MemberEnd() || !version->value.IsInt() ||
        (version->value.GetInt() != 1 && version->value.GetInt() != 2))
        return Fail(error, "version", "version must be integer1 or2");
    if (version->value.GetInt() == 2)
    {
        if (!Fields(document, std::array<std::string_view, 2>{"version", "features"}, "root", error)) return false;
        Draft draft;
        return ReadGrouped(document["features"], draft, error) && draft.Finish(output, error);
    }
    if (!Fields(document, std::array<std::string_view, 4>{"version", "features", "values", "comments"}, "root", error)) return false;
    if (!Fields(document["comments"], std::array<std::string_view, 2>{"features", "values"}, "comments", error)) return false;
    Draft draft;
    return ReadEntries<true>(document["features"], draft, false, error) &&
        ReadEntries<false>(document["values"], draft, false, error) &&
        ReadEntries<true>(document["comments"]["features"], draft, true, error) &&
        ReadEntries<false>(document["comments"]["values"], draft, true, error) && draft.Finish(output, error);
}
std::string Serialize(const Snapshot& snapshot)
{
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    const auto key = [&](std::string_view text) { writer.Key(text.data(), static_cast<rapidjson::SizeType>(text.size())); };
    const auto text = [&](std::string_view value) { writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size())); };
    writer.StartObject(); key("version"); writer.Int(2);
    key("features"); writer.StartObject();
    for (const auto& feature : FeatureRows)
    {
        key(feature.key); writer.StartObject();
        key("enabled"); writer.Bool(snapshot.Enabled(feature.id));
        key("comment"); text(snapshot.Comment(feature.id));
        key("values"); writer.StartObject();
        for (const auto& descriptor : SettingRows)
        {
            if (descriptor.feature != feature.id) continue;
            key(descriptor.key); writer.StartObject(); key("value");
            if (const auto number = snapshot.Number(descriptor.id))
            {
                if (descriptor.type == ValueType::Integer) writer.Int(static_cast<int>(*number));
                else writer.Double(*number);
            }
            else writer.Null();
            key("comment"); text(snapshot.Comment(descriptor.id)); writer.EndObject();
        }
        writer.EndObject(); writer.EndObject();
    }
    writer.EndObject(); writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize()) + '\n';
}
}
