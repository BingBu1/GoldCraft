// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"
#include "../../../guest/cof/light_equipment.hpp"

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
constexpr std::array<BlindBoxDescriptor, BlindBoxCount> BoxRows{{
    {BlindBox::Cof, Setting::BlindBoxPrice1, "cof", "CoF武器补给；weight为相对权重，0停用；已持有或本图未就绪的武器不参与抽取。"},
    {BlindBox::CounterStrike, Setting::BlindBoxPrice2, "cs16", "CS1.6武器补给；同类有效武器按权重抽取，无可领取武器时不扣钱。"},
    {BlindBox::Csol, Setting::BlindBoxPrice3, "csol", "CSOL武器补给；每次发放一把主武器和一把副武器，两个槽位均可领取时才扣费。启用并有可领取的近战武器时，按配置权重额外抽取一把。"}
}};
constexpr std::string_view CofDefaults[]{"cof_glock", "cof_p345", "cof_revolver", "cof_vp70", "cof_tmp", "cof_mp5", "cof_m16", "cof_g43", "cof_shotgun"};
constexpr std::string_view CsDefaults[]{"p228", "elites", "fn57", "usp", "glock", "deagle", "xm1014", "m3",
    "mac10", "ump45", "mp5", "tmp", "p90", "scout", "aug", "sg550", "galil", "famas", "awp", "m4a1", "g3sg1", "sg552", "ak47", "m249", "knife"};
constexpr std::string_view CsolDefaults[]{"tknifeex2", "skull1", "skull2", "skull3", "skull5", "balrog11", "k3", "m249ex",
    "cannon", "beamgun", "beamgunle", "dragonsword", "skullaxe"};
constexpr std::array<FeatureDescriptor, FeatureCount> FeatureRows{{
    {Feature::Kick, "kick", "csol_custom_kick", "F范围踢击开关；关闭不删除配置，可再次开启。", true, Scope::Immediate},
    {Feature::TimeCycle, "time_cycle", "csol_custom_time_cycle", "即时开关时间阶段；开启从正午开始，关闭恢复地图环境；配套资源须已预载。", false, Scope::Immediate},
    {Feature::BlindBox, "blind_box", "csol_custom_blind_box", "CoF/CS1.6/CSOL来源补给及手枪、近战类别补给在全图使用CS金钱购买。pools设置每把武器的相对权重，0停用。", true, Scope::Immediate},
    {Feature::Mastery, "mastery", "csol_custom_mastery", "本图熟练度100/200/300；一档换弹，二档换弹和伤害，三档射速和伤害。", false, Scope::NextMapSession},
    {Feature::CofSprint, "cof_sprint", "csol_custom_cof_sprint", "下次载入地图启用体力。正午为灾变原有移速，黄昏和永夜解锁疾跑，站姿疾跑地速为300。", false, Scope::NextMapSession},
    {Feature::Blood, "blood", "csol_custom_blood", "僵尸受击后保留随机模型血迹，近距离枪击将血溅到射手手臂及手持枪械。血迹持续到对应实体离开本图，关闭时隐藏。", false, Scope::Immediate},
    {Feature::DuskBuff, "dusk_buff", "csol_custom_dusk_buff", "黄昏普通怪的基础移速和回血；开关和数值在本图即时生效。", false, Scope::Immediate},
    {Feature::NightBuff, "night_buff", "csol_custom_night_buff", "永夜普通怪的基础伤害、移速和回血；开关和数值在本图即时生效。", false, Scope::Immediate},
    {Feature::ZombiePopulation, "zombie_population", "csol_custom_zombie_population", "阶段普通怪上限为基础值加每位真人增量；下一次刷怪时生效。", false, Scope::Immediate},
    {Feature::RandomHorde, "random_horde", "csol_custom_random_horde", "永夜随机尸潮；阶段怪量开启后，尸潮期间放宽普通怪上限。", false, Scope::Immediate},
    {Feature::DamageRefundCap, "damage_refund_cap", "csol_custom_damage_refund_cap", "返现上限对每位玩家与每只怪物的组合分别累计，新怪租约获得新额度。", false, Scope::NextMapSession},
    {Feature::NightPlayerLights, "night_player_lights", "csol_custom_night_player_lights", "永夜全员互见随身光；客户端管理灯池与跟随，服务器仅同步阶段和配置。自定义默认开启。", true, Scope::Immediate},
    {Feature::ZombieGibs, "zombie_gibs", "csol_custom_zombie_gibs", "开启僵尸爆尸。玩家击杀普通僵尸或精英时喷出4到5块碎片，尸体立即消失。当前地图即时生效。", true, Scope::Immediate},
    {Feature::NightEquipment, "night_equipment", "csol_custom_night_equipment", "允许玩家在永夜期间购买提灯和照明弹。", true, Scope::Immediate},
    {Feature::AmmoRefill, "ammo_refill", "csol_custom_ammo_refill", "允许存活玩家在购买区一次补满随身主副武器的弹匣和备弹。", true, Scope::Immediate}
}};

// Bounds are host configuration/overflow limits, not recovered CSOL values.
// Phase times/thresholds are user-specified. Other defaults below are explicit
// GoldCraft custom balance decisions, not recovered CSOL values. Unknown CoF
// base parameters remain absent instead of silently defining partial behavior.
constexpr std::array<SettingDescriptor, SettingCount> SettingRows{{
    {Setting::MiddaySeconds, Feature::TimeCycle, "midday_seconds", "csol_custom_midday_seconds", "正午持续秒数；即时调整当前正午剩余时间，保留已过时间。", ValueType::Integer, 1, 86400, 180, Scope::Immediate},
    {Setting::DuskSeconds, Feature::TimeCycle, "dusk_seconds", "csol_custom_dusk_seconds", "黄昏持续秒数；即时调整当前黄昏剩余时间，阶段不倒退。", ValueType::Integer, 1, 86400, 120, Scope::Immediate},
    {Setting::BlindBoxPrice1, Feature::BlindBox, "blind_box_price_1", "csol_custom_blind_box_price_1", "第一档CS金钱价格，三档严格递增。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 3000, Scope::Immediate},
    {Setting::BlindBoxPrice2, Feature::BlindBox, "blind_box_price_2", "csol_custom_blind_box_price_2", "第二档CS金钱价格，不用仓库币。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 6000, Scope::Immediate},
    {Setting::BlindBoxPrice3, Feature::BlindBox, "blind_box_price_3", "csol_custom_blind_box_price_3", "第三档CS金钱价格。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 30000, Scope::Immediate},
    {Setting::MasteryThreshold1, Feature::Mastery, "mastery_threshold_1", "csol_custom_mastery_threshold_1", "本图累计熟练度第一档阈值。", ValueType::Integer, 1, 1000000000, 100, Scope::NextMapSession},
    {Setting::MasteryThreshold2, Feature::Mastery, "mastery_threshold_2", "csol_custom_mastery_threshold_2", "本图累计熟练度第二档阈值，严格大于第一档。", ValueType::Integer, 1, 1000000000, 200, Scope::NextMapSession},
    {Setting::MasteryThreshold3, Feature::Mastery, "mastery_threshold_3", "csol_custom_mastery_threshold_3", "本图累计熟练度第三档阈值，严格大于第二档。", ValueType::Integer, 1, 1000000000, 300, Scope::NextMapSession},
    {Setting::MasteryReload1, Feature::Mastery, "mastery_reload_1", "csol_custom_mastery_reload_1", "一档换弹时长倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, .85, Scope::NextMapSession},
    {Setting::MasteryReload2, Feature::Mastery, "mastery_reload_2", "csol_custom_mastery_reload_2", "二档换弹时长倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, .7, Scope::NextMapSession},
    {Setting::MasteryDamage2, Feature::Mastery, "mastery_damage_2", "csol_custom_mastery_damage_2", "二档伤害倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.10, Scope::NextMapSession},
    {Setting::MasteryDamage3, Feature::Mastery, "mastery_damage_3", "csol_custom_mastery_damage_3", "三档伤害倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.20, Scope::NextMapSession},
    {Setting::MasteryFireRate3, Feature::Mastery, "mastery_fire_rate_3", "csol_custom_mastery_fire_rate_3", "三档每秒射击频率倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.10, Scope::NextMapSession},
    {Setting::SprintMaximum, Feature::CofSprint, "sprint_maximum", "csol_custom_sprint_maximum", "有限阶段体力上限，正午无限由阶段规则表达。", ValueType::Real, .001, 1000000, {}, Scope::NextMapSession},
    {Setting::SprintDrainPerSecond, Feature::CofSprint, "sprint_drain_per_second", "csol_custom_sprint_drain_per_second", "已停用的旧CoF每秒消耗；启用绝命档案前须置null，不能换算成位移单位。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::SprintRecoveryPerSecond, Feature::CofSprint, "sprint_recovery_per_second", "csol_custom_sprint_recovery_per_second", "可选自定义每秒恢复；null使用原版延迟3秒和加速恢复。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::SprintSpeed, Feature::CofSprint, "sprint_speed", "csol_custom_sprint_speed", "已停用的旧固定跑速；启用前须置null，站姿疾跑地速为300。", ValueType::Real, .001, 4096, {}, Scope::NextMapSession},
    {Setting::WalkSpeed, Feature::CofSprint, "walk_speed", "csol_custom_walk_speed", "已停用的旧固定走速；启用前须置null，原版按武器速度乘0.5。", ValueType::Real, .001, 4096, {}, Scope::NextMapSession},
    {Setting::JumpCost, Feature::CofSprint, "jump_cost", "csol_custom_jump_cost", "跳跃单次体力消耗。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::KickCost, Feature::CofSprint, "kick_cost", "csol_custom_kick_cost", "踢击单次体力消耗，接受动作后由权威消费者记账。", ValueType::Real, 0, 1000000, {}, Scope::NextMapSession},
    {Setting::DuskMaximumMultiplier, Feature::CofSprint, "dusk_maximum_multiplier", "csol_custom_dusk_maximum_multiplier", "黄昏体力上限倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.5, Scope::NextMapSession},
    {Setting::DuskRecoveryMultiplier, Feature::CofSprint, "dusk_recovery_multiplier", "csol_custom_dusk_recovery_multiplier", "黄昏体力恢复倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.25, Scope::NextMapSession},
    {Setting::DuskSpeedMultiplier, Feature::DuskBuff, "dusk_speed_multiplier", "csol_custom_dusk_speed_multiplier", "黄昏基础移速倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.05, Scope::Immediate},
    {Setting::DuskRegenPerSecond, Feature::DuskBuff, "dusk_regen_per_second", "csol_custom_dusk_regen_per_second", "黄昏每秒回血，0为无额外回血。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000000, 0, Scope::Immediate},
    {Setting::NightDamageMultiplier, Feature::NightBuff, "night_damage_multiplier", "csol_custom_night_damage_multiplier", "永夜基础伤害倍率。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000, 1.20, Scope::Immediate},
    {Setting::NightSpeedMultiplier, Feature::NightBuff, "night_speed_multiplier", "csol_custom_night_speed_multiplier", "永夜基础移速倍率，1不变。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, .001, 1000, 1.10, Scope::Immediate},
    {Setting::NightRegenPerSecond, Feature::NightBuff, "night_regen_per_second", "csol_custom_night_regen_per_second", "永夜每秒回血，0为无额外回血。GoldCraft自定义默认，非CSOL原值。", ValueType::Real, 0, 1000000, 0, Scope::Immediate},
    {Setting::MiddayZombieBase, Feature::ZombiePopulation, "midday_zombie_base", "csol_custom_midday_zombie_base", "正午普通怪基础上限；最终受地图普通怪池限制。", ValueType::Integer, 0, 4096, {}, Scope::Immediate},
    {Setting::DuskZombieBase, Feature::ZombiePopulation, "dusk_zombie_base", "csol_custom_dusk_zombie_base", "黄昏普通怪基础上限。", ValueType::Integer, 0, 4096, {}, Scope::Immediate},
    {Setting::NightZombieBase, Feature::ZombiePopulation, "night_zombie_base", "csol_custom_night_zombie_base", "永夜普通怪基础上限。", ValueType::Integer, 0, 4096, {}, Scope::Immediate},
    {Setting::ZombiesPerPlayer, Feature::ZombiePopulation, "zombies_per_player", "csol_custom_zombies_per_player", "每位已加入战斗队伍的真人增加的普通怪上限。", ValueType::Integer, 0, 4096, {}, Scope::Immediate},
    {Setting::HordeIntervalSeconds, Feature::RandomHorde, "horde_interval_seconds", "csol_custom_horde_interval_seconds", "永夜首次重复刷怪、上次未触发或上次尸潮结束后的抽样间隔，单位秒。", ValueType::Real, .001, 86400, {}, Scope::Immediate},
    {Setting::HordeProbability, Feature::RandomHorde, "horde_probability", "csol_custom_horde_probability", "每次抽样尸潮概率，取值0至1。", ValueType::Real, 0, 1, {}, Scope::Immediate},
    {Setting::HordeDurationSeconds, Feature::RandomHorde, "horde_duration_seconds", "csol_custom_horde_duration_seconds", "单次尸潮放宽普通怪上限的持续秒数。", ValueType::Real, .001, 86400, {}, Scope::Immediate},
    {Setting::DamageRefundMaximumPerMonster, Feature::DamageRefundCap, "damage_refund_max_per_monster", "csol_custom_damage_refund_max_per_monster", "每player-victim组合累计CS金钱上限，怪物新租约是新额度。GoldCraft自定义默认，非CSOL原值。", ValueType::Integer, 0, 32000, 1000, Scope::NextMapSession},
    {Setting::DuskLightLevel, Feature::TimeCycle, "dusk_light_level", "csol_custom_dusk_light_level", "黄昏光照字符的ASCII值，97最暗到122最亮；默认105为原版i档。", ValueType::Integer, 97, 122, 105, Scope::NextMapSession},
    {Setting::NightLightLevel, Feature::TimeCycle, "night_light_level", "csol_custom_night_light_level", "永夜最终光照档，不得高于黄昏；按用户要求默认98为更暗b档，99保留原版c档。", ValueType::Integer, 97, 122, 98, Scope::NextMapSession},
    {Setting::LightStepSeconds, Feature::TimeCycle, "light_step_seconds", "csol_custom_light_step_seconds", "永夜逐次降低一档的间隔秒数；默认原版2.5，超时不追赶多档。", ValueType::Real, .001, 3600, 2.5, Scope::NextMapSession},
    {Setting::NightFogDensity, Feature::TimeCycle, "night_fog_density", "csol_custom_night_fog_density", "永夜最暗档的雾密度；0关闭。默认原版float位型3A378034。", ValueType::Real, 0, .01, 0.000699999975040555, Scope::NextMapSession},
    {Setting::NightFogColor, Feature::TimeCycle, "night_fog_color", "csol_custom_night_fog_color", "雾颜色的十进制RGB打包值0xRRGGBB；默认65793对应原版(1,1,1)。", ValueType::Integer, 0, 16777215, 65793, Scope::NextMapSession},
    {Setting::NightFogStart, Feature::TimeCycle, "night_fog_start", "csol_custom_night_fog_start", "标准Fog接口起始距离，默认原版100，须小于结束距离。", ValueType::Real, 0, 999999, 100, Scope::NextMapSession},
    {Setting::NightFogEnd, Feature::TimeCycle, "night_fog_end", "csol_custom_night_fog_end", "标准Fog接口结束距离，默认原版2000，须大于起始距离。", ValueType::Real, .001, 1000000, 2000, Scope::NextMapSession},
    {Setting::NightFogSkybox, Feature::TimeCycle, "night_fog_skybox", "csol_custom_night_fog_skybox", "雾是否影响天空盒：0否，1是；默认原版1。", ValueType::Integer, 0, 1, 1, Scope::NextMapSession},
    {Setting::SprintCostPerUnit, Feature::CofSprint, "sprint_cost_per_unit", "csol_custom_sprint_cost_per_unit", "绝命档案按水平实际位移扣费，每GoldSrc单位体力；默认原版0.1，非每秒。", ValueType::Real, 0, 1000000, .1, Scope::NextMapSession},
    {Setting::NightPlayerLightRadius, Feature::NightPlayerLights, "night_player_light_radius", "csol_custom_night_player_light_radius", "随身光半径，GoldSrc单位16至512；自定义默认128，客户端更新位置。", ValueType::Real, 16, 512, 128, Scope::Immediate},
    {Setting::NightPlayerLightBrightness, Feature::NightPlayerLights, "night_player_light_brightness", "csol_custom_night_player_light_brightness", "随身光亮度0至255，0熄灭；自定义默认48，保留永夜黑暗和雾。", ValueType::Integer, 0, 255, 48, Scope::Immediate},
    {Setting::NightPlayerLightColor, Feature::NightPlayerLights, "night_player_light_color", "csol_custom_night_player_light_color", "随身光RGB颜色十进制0至16777215；自定义默认16777215为白色。", ValueType::Integer, 0, 16777215, 16777215, Scope::Immediate},
    {Setting::HordeExtraZombies, Feature::RandomHorde, "horde_extra_zombies", "csol_custom_horde_extra_zombies", "尸潮期间增加的普通怪上限，默认8；仍受地图普通怪池限制。", ValueType::Integer, 0, 4096, 8, Scope::Immediate},
    {Setting::BloodSplashRadius, Feature::Blood, "blood_splash_radius", "csol_custom_blood_splash_radius", "血迹溅到攻击者的最大距离，默认128单位，0只在僵尸模型上显示。", ValueType::Real, 0, 1024, 128, Scope::Immediate},
    {Setting::ZombieGibChance, Feature::ZombieGibs, "zombie_gib_chance", "csol_custom_zombie_gib_chance", "普通击杀的爆尸概率（%）。", ValueType::Integer, 0, 100, 2, Scope::Immediate},
    {Setting::ZombieGibMeleeHeadChance, Feature::ZombieGibs, "zombie_gib_melee_head_chance", "csol_custom_zombie_gib_melee_head_chance", "近战武器命中头部并击杀时的爆尸概率（%）。", ValueType::Integer, 0, 100, 50, Scope::Immediate},
    {Setting::NightLanternPrice, Feature::NightEquipment, "night_lantern_price", "csol_custom_night_lantern_price", "每盏提灯的CS金钱价格。", ValueType::Integer, 0, 32000, guest::cof::light::Lantern.price, Scope::Immediate},
    {Setting::NightFlarePrice, Feature::NightEquipment, "night_flare_price", "csol_custom_night_flare_price", "每支照明弹的CS金钱价格。", ValueType::Integer, 0, 32000, guest::cof::light::Flare.price, Scope::Immediate},
    {Setting::AmmoRefillPrice, Feature::AmmoRefill, "ammo_refill_price", "csol_custom_ammo_refill_price", "主副武器一次满弹的CS金钱总价。已有满弹无需扣款。", ValueType::Integer, 0, 32000, 2000, Scope::Immediate}
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
unsigned RetiredBloodSetting(std::string_view key) noexcept
{
    if (key == "blood_lifetime_seconds") return 1;
    if (key == "blood_maximum") return 2;
    return 0;
}

bool ReadRetiredBlood(unsigned bit, unsigned& seen, const rapidjson::Value& value,
    std::string_view key, bool comment, Error& error)
{
    if (seen & bit) return Fail(error, key, "duplicate retired setting");
    seen |= bit;
    if (comment ? !value.IsString() : !(value.IsNull() || value.IsNumber()))
        return Fail(error, key, "invalid retired blood setting");
    return true;
}

template<bool FeatureKeys>
bool ReadEntries(const rapidjson::Value& object, Draft& draft, bool comments, Error& error)
{
    if (!object.IsObject()) return Fail(error, comments ? "comments" : (FeatureKeys ? "features" : "values"), "expected an object");
    constexpr auto count = FeatureKeys ? FeatureCount : SettingCount;
    std::array<bool, count> seen{};
    unsigned retired{};
    for (const auto& member : object.GetObject())
    {
        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
        if constexpr (FeatureKeys)
        {
            // Older operator files retained the retired whole-map purchase lock.
            if (key == "ordinary_purchase")
            {
                if (retired++) return Fail(error, key, "duplicate feature identity");
                if (comments ? !member.value.IsString() : !member.value.IsBool())
                    return Fail(error, key, "invalid legacy purchase setting");
                continue;
            }
        }
        const auto id = [&]
        {
            if constexpr (FeatureKeys) return FindFeature(key);
            else return FindSetting(key);
        }();
        if constexpr (!FeatureKeys)
        {
            if (const auto bit = RetiredBloodSetting(key))
            {
                if (!ReadRetiredBlood(bit, retired, member.value, key, comments, error)) return false;
                continue;
            }
        }
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
bool ReadPools(const rapidjson::Value& object, Draft& draft, Error& error)
{
    if (!object.IsObject()) return Fail(error, "pools", "expected an object");
    std::array<bool, BlindBoxCount> seen{};
    for (const auto& member : object.GetObject())
    {
        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
        const auto box = FindBlindBox(key);
        if (!box) return Fail(error, key, "unknown blind box identity");
        const auto index = static_cast<std::size_t>(*box);
        if (seen[index]) return Fail(error, key, "duplicate blind box identity");
        seen[index] = true;
        const auto& pool = member.value;
        if (!Fields(pool, std::array<std::string_view, 2>{"comment", "weapons"}, key, error)) return false;
        if (!pool["comment"].IsString()) return Fail(error, key, "explanation must be a string");
        const auto& weapons = pool["weapons"];
        if (!weapons.IsArray() || weapons.Size() > MaximumBlindBoxEntries)
            return Fail(error, key, "pool requires an array of at most64 weapons");
        std::vector<BlindBoxEntry> entries;
        entries.reserve(weapons.Size());
        for (const auto& entry : weapons.GetArray())
        {
            if (!Fields(entry, std::array<std::string_view, 2>{"weapon", "weight"}, key, error)) return false;
            if (!entry["weapon"].IsString() || !entry["weight"].IsUint())
                return Fail(error, key, "weapon requires a catalog alias and weight requires a nonnegative integer");
            entries.push_back({std::string(entry["weapon"].GetString(), entry["weapon"].GetStringLength()), entry["weight"].GetUint()});
        }
        if (!draft.SetPool(*box, entries, error) ||
            !draft.SetComment(*box, {pool["comment"].GetString(), pool["comment"].GetStringLength()}, error)) return false;
    }
    return true;
}
bool ReadGrouped(const rapidjson::Value& object, Draft& draft, Error& error)
{
    if (!object.IsObject()) return Fail(error, "features", "expected an object");
    std::array<bool, FeatureCount> seen{};
    bool retiredPurchase{};
    for (const auto& member : object.GetObject())
    {
        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
        if (key == "ordinary_purchase")
        {
            if (retiredPurchase) return Fail(error, key, "duplicate feature identity");
            retiredPurchase = true;
            const auto& group = member.value;
            if (!Fields(group, std::array<std::string_view, 3>{"enabled", "comment", "values"}, key, error))
                return false;
            if (!group["enabled"].IsBool() || !group["comment"].IsString() ||
                !group["values"].IsObject() || group["values"].MemberCount())
                return Fail(error, key, "invalid legacy purchase setting");
            continue;
        }
        const auto feature = FindFeature(key);
        if (!feature) return Fail(error, key, "unknown feature identity");
        const auto index = static_cast<std::size_t>(*feature);
        if (seen[index]) return Fail(error, key, "duplicate feature identity");
        seen[index] = true;
        const auto& group = member.value;
        const bool pools = *feature == Feature::BlindBox && group.IsObject() && group.HasMember("pools");
        if (pools ? !Fields(group, std::array<std::string_view, 4>{"enabled", "comment", "values", "pools"}, key, error) :
                    !Fields(group, std::array<std::string_view, 3>{"enabled", "comment", "values"}, key, error)) return false;
        if (!group["enabled"].IsBool()) return Fail(error, key, "enabled must be a JSON boolean");
        if (!group["comment"].IsString()) return Fail(error, key, "explanation must be a string");
        if (!draft.Set(*feature, group["enabled"].GetBool(), error) ||
            !draft.SetComment(*feature, {group["comment"].GetString(), group["comment"].GetStringLength()}, error)) return false;
        const auto& values = group["values"];
        if (!values.IsObject()) return Fail(error, key, "values must be an object");
        std::array<bool, SettingCount> valueSeen{};
        unsigned retired{};
        for (const auto& entry : values.GetObject())
        {
            const std::string_view name(entry.name.GetString(), entry.name.GetStringLength());
            if (*feature == Feature::Blood)
            {
                if (const auto bit = RetiredBloodSetting(name))
                {
                    const auto& leaf = entry.value;
                    if (!Fields(leaf, std::array<std::string_view, 2>{"value", "comment"}, name, error)) return false;
                    if (!leaf["comment"].IsString()) return Fail(error, name, "explanation must be a string");
                    if (!ReadRetiredBlood(bit, retired, leaf["value"], name, false, error)) return false;
                    continue;
                }
            }
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
                if (descriptor.defaultValue)
                    return Fail(error, name, "null is only valid for an optional setting without a default");
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
        if (pools && !ReadPools(group["pools"], draft, error)) return false;
    }
    return true;
}
}

std::span<const FeatureDescriptor, FeatureCount> Features() noexcept { return FeatureRows; }
std::span<const SettingDescriptor, SettingCount> Settings() noexcept { return SettingRows; }
std::span<const BlindBoxDescriptor, BlindBoxCount> BlindBoxes() noexcept { return BoxRows; }
std::optional<BlindBox> FindBlindBox(std::string_view key) noexcept
{
    for (const auto& descriptor : BoxRows) if (descriptor.key == key) return descriptor.id;
    return {};
}
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
std::span<const BlindBoxEntry> Snapshot::Pool(BlindBox box) const noexcept
{ const auto index = static_cast<std::size_t>(box); return index < pools_.size() ? std::span<const BlindBoxEntry>(pools_[index]) : std::span<const BlindBoxEntry>{}; }
std::string_view Snapshot::Comment(BlindBox box) const noexcept
{ const auto index = static_cast<std::size_t>(box); return index < poolComments_.size() ? poolComments_[index] : std::string_view{}; }

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
    const std::array<std::span<const std::string_view>, BlindBoxCount> defaults{CofDefaults, CsDefaults, CsolDefaults};
    for (std::size_t i = 0; i < defaults.size(); ++i)
    {
        value_.poolComments_[i] = BoxRows[i].comment;
        value_.pools_[i].reserve(defaults[i].size());
        for (const auto alias : defaults[i]) value_.pools_[i].push_back({std::string(alias), 1});
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
bool Draft::SetPool(BlindBox box, std::span<const BlindBoxEntry> entries, Error& error)
{
    const auto index = static_cast<std::size_t>(box);
    if (index >= BlindBoxCount) return Fail(error, "pools", "invalid blind box identity");
    if (entries.size() > MaximumBlindBoxEntries) return Fail(error, BoxRows[index].key, "pool exceeds64 weapons");
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        if (entry.weapon.empty() || entry.weapon.size() > 63 || entry.weight > MaximumBlindBoxWeight ||
            !std::all_of(entry.weapon.begin(), entry.weapon.end(), [](char c) { return
                (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }))
            return Fail(error, BoxRows[index].key, "invalid catalog alias or weight outside0..1000000");
        for (std::size_t j = 0; j < i; ++j)
            if (entries[j].weapon == entry.weapon) return Fail(error, BoxRows[index].key, "duplicate weapon alias");
    }
    value_.pools_[index].assign(entries.begin(), entries.end()); error = {}; return true;
}
bool Draft::SetComment(BlindBox box, std::string_view text, Error& error)
{
    const auto index = static_cast<std::size_t>(box);
    if (index >= BlindBoxCount) return Fail(error, "pools", "invalid blind box identity");
    if (!TextValid(text)) return Fail(error, BoxRows[index].key, "invalid pool explanation");
    value_.poolComments_[index] = text; error = {}; return true;
}
bool Draft::Finish(SnapshotPtr& output, Error& error) const
{
    for (const auto& descriptor : SettingRows)
        if (value_.Enabled(descriptor.feature) && descriptor.feature != Feature::CofSprint && !value_.Number(descriptor.id))
            return Fail(error, descriptor.key, "enabled feature requires an explicit numeric parameter");
    if (value_.Enabled(Feature::CofSprint))
        for (const auto obsolete : {Setting::SprintDrainPerSecond, Setting::SprintSpeed, Setting::WalkSpeed})
            if (value_.Number(obsolete))
                return Fail(error, SettingRows[static_cast<std::size_t>(obsolete)].key,
                    "legacy CoF parameter must be null; StandAlone uses distance cost and weapon-derived speed");
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
        return Fail(error, "root", "expected nonempty UTF-8 configuration within the size limit and without NUL");
    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseCommentsFlag |
        rapidjson::kParseIterativeFlag | rapidjson::kParseFullPrecisionFlag>(json.data(), json.size());
    if (document.HasParseError())
    { error = {document.GetErrorOffset(), "root", "invalid JSON or UTF8 encoding"}; return false; }
    if (!document.IsObject()) return Fail(error, "root", "expected an object");
    const auto version = document.FindMember("version");
    if (version == document.MemberEnd() || !version->value.IsInt() ||
        version->value.GetInt() < 1 || version->value.GetInt() > 3)
        return Fail(error, "version", "version must be integer1, 2 or3");
    const auto format = version->value.GetInt();
    Draft draft;
    // Parse partial old documents against their original numeric defaults.
    // Upgrade only a complete former default tuple after resolving omissions.
    if (format < 3 && !draft.Set(Setting::BlindBoxPrice3, 9000, error)) return false;
    if (format == 1)
    {
        if (!Fields(document, std::array<std::string_view, 4>{"version", "features", "values", "comments"}, "root", error) ||
            !Fields(document["comments"], std::array<std::string_view, 2>{"features", "values"}, "comments", error) ||
            !ReadEntries<true>(document["features"], draft, false, error) ||
            !ReadEntries<false>(document["values"], draft, false, error) ||
            !ReadEntries<true>(document["comments"]["features"], draft, true, error) ||
            !ReadEntries<false>(document["comments"]["values"], draft, true, error)) return false;
    }
    else if (!Fields(document, std::array<std::string_view, 2>{"version", "features"}, "root", error) ||
        !ReadGrouped(document["features"], draft, error)) return false;

    if (format == 3) return draft.Finish(output, error);
    SnapshotPtr legacy;
    if (!draft.Finish(legacy, error)) return false;
    if (legacy->Integer(Setting::BlindBoxPrice1) == 3000 &&
        legacy->Integer(Setting::BlindBoxPrice2) == 6000 && legacy->Integer(Setting::BlindBoxPrice3) == 9000 &&
        !draft.Set(Setting::BlindBoxPrice3, {}, error)) return false;
    return draft.Finish(output, error);
}
std::string Serialize(const Snapshot& snapshot)
{
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    const auto key = [&](std::string_view text) { writer.Key(text.data(), static_cast<rapidjson::SizeType>(text.size())); };
    const auto text = [&](std::string_view value) { writer.String(value.data(), static_cast<rapidjson::SizeType>(value.size())); };
    writer.StartObject(); key("version"); writer.Int(3);
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
        writer.EndObject();
        if (feature.id == Feature::BlindBox)
        {
            key("pools"); writer.StartObject();
            for (const auto& box : BoxRows)
            {
                key(box.key); writer.StartObject();
                key("comment"); text(snapshot.Comment(box.id));
                key("weapons"); writer.StartArray();
                for (const auto& entry : snapshot.Pool(box.id))
                {
                    writer.StartObject(); key("weapon"); text(entry.weapon);
                    key("weight"); writer.Uint(entry.weight); writer.EndObject();
                }
                writer.EndArray(); writer.EndObject();
            }
            writer.EndObject();
        }
        writer.EndObject();
    }
    writer.EndObject(); writer.EndObject();
    return std::string(buffer.GetString(), buffer.GetSize()) + '\n';
}
}
