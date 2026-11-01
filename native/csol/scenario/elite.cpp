// SPDX-License-Identifier: GPL-3.0-or-later
#include "elite.hpp"
#include "monster_appearances.hpp"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace csol::scenario
{
#pragma clang fp contract(off)
namespace
{
const rapidjson::Value* Member(const rapidjson::Value& value, const char* name)
{
    if (!value.IsObject()) return nullptr;
    const auto found = value.FindMember(name);
    return found == value.MemberEnd() ? nullptr : &found->value;
}

bool Integer(const rapidjson::Value& value, std::int32_t& output)
{
    if (value.IsBool()) { output = value.GetBool() ? 1 : 0; return true; }
    if (!value.IsNumber()) return false;
    const auto number = std::trunc(value.GetDouble());
    if (!std::isfinite(number) || number < INT32_MIN || number > INT32_MAX) return false;
    output = static_cast<std::int32_t>(number);
    return true;
}

bool IntegerField(const rapidjson::Value& object, const char* name, std::int32_t& output)
{
    const auto* value = Member(object, name);
    return !value || Integer(*value, output);
}

bool Parameters(const rapidjson::Value& value, EliteSkillParameters::Values& output)
{
    if (!value.IsObject()) return false;
    for (const auto& member : value.GetObject())
    {
        if (!member.value.IsNumber() && !member.value.IsBool()) return false;
        const double number = member.value.IsBool() ? (member.value.GetBool() ? 1.0 : 0.0) :
            member.value.GetDouble();
        const float narrowed = static_cast<float>(number);
        if (!std::isfinite(number) || !std::isfinite(narrowed)) return false;
        output.emplace(std::string(member.name.GetString(), member.name.GetStringLength()), narrowed);
    }
    return true;
}

std::int16_t Short(std::int32_t value)
{
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}
}

float EliteSettings::Parameter(int skill, int difficulty, std::string_view name) const
{
    if (skill < 0 || static_cast<std::size_t>(skill) >= skills.size()) return 0;
    const auto& entry = skills[skill];
    if (const auto level = entry.difficulties.find(difficulty); level != entry.difficulties.end())
        if (const auto parameter = level->second.find(name); parameter != level->second.end())
            return parameter->second;
    const auto parameter = entry.base.find(name);
    return parameter == entry.base.end() ? 0 : parameter->second;
}

bool ReadEliteSettings(std::string_view json, EliteSettings& output, ConfigError& error)
{
    error = {};
    const auto fail = [&](const char* message) { error.message = message; return false; };
    if (json.empty() || json.size() > 8 * 1024 * 1024 || json.find('\0') != json.npos)
        return fail("invalid elite JSON size or embedded NUL");
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError())
    { error = {document.GetErrorOffset(), rapidjson::GetParseError_En(document.GetParseError())}; return false; }
    const auto* elite = Member(document, "ZBS_Elite");
    const auto* properties = elite ? Member(*elite, "Properties") : nullptr;
    const auto* colors = elite ? Member(*elite, "Colors") : nullptr;
    const auto* skills = elite ? Member(*elite, "Skills") : nullptr;
    if (!properties || !properties->IsArray() || !colors || !colors->IsArray() ||
        colors->Size() < 3 || !skills || !skills->IsArray())
        return fail("elite settings require Properties, three Colors and Skills arrays");

    EliteSettings next;
    for (const auto& entry : properties->GetArray())
    {
        if (!entry.IsObject()) return fail("elite property record must be an object");
        const auto* difficulty = Member(entry, "difficulty");
        if (!difficulty) continue; // Original375079A4 skips this record.
        EliteProperties row;
        if (!Integer(*difficulty, row.difficulty) ||
            !IntegerField(entry, "attackDamage", row.attackDamage) ||
            !IntegerField(entry, "attackRange", row.attackRange) ||
            !IntegerField(entry, "attackInterval", row.attackInterval) ||
            !IntegerField(entry, "maxHealth", row.maxHealth) ||
            !IntegerField(entry, "maxArmor", row.maxArmor) ||
            !IntegerField(entry, "maxSpeed", row.maxSpeed) ||
            !IntegerField(entry, "bonusMoney", row.bonusMoney) ||
            !IntegerField(entry, "killType", row.killType))
            return fail("elite property is not a finite int32 value");
        next.properties.emplace(row.difficulty, row);
    }
    for (rapidjson::SizeType i = 0; i < 3; ++i)
        if (!Integer((*colors)[i], next.colors[i]))
            return fail("elite color is not a finite int32 value");
    next.skills.reserve(skills->Size());
    for (const auto& entry : skills->GetArray())
    {
        if (!entry.IsObject()) return fail("elite skill record must be an object");
        EliteSkillParameters parameters;
        if (const auto* base = Member(entry, "Params"); base && !Parameters(*base, parameters.base))
            return fail("elite Params must contain finite numeric values");
        for (int difficulty = 0; difficulty < 10; ++difficulty)
        {
            const auto name = "Params" + std::to_string(difficulty);
            if (const auto* overrides = Member(entry, name.c_str()))
            {
                EliteSkillParameters::Values values;
                if (!Parameters(*overrides, values))
                    return fail("elite difficulty Params must contain finite numeric values");
                parameters.difficulties.emplace(difficulty, std::move(values));
            }
        }
        next.skills.push_back(std::move(parameters));
    }
    output = std::move(next);
    return true;
}

bool MakeEliteMonsterType(const EliteSettings& settings, int difficulty,
    std::shared_ptr<const MonsterType>& output, ConfigError& error)
{
    error = {};
    const auto found = settings.properties.find(difficulty);
    if (difficulty <= 0 || static_cast<std::size_t>(difficulty) > settings.properties.size() ||
        found == settings.properties.end())
    { error.message = "elite difficulty has no original property record"; return false; }
    if (!FindMonsterAppearance("zbs_elite_01"))
    { error.message = "elite appearance is unavailable"; return false; }
    const auto& properties = found->second;
    auto type = std::make_shared<MonsterType>();
    type->id = 32800;
    type->appearance = "zbs_elite_01";
    type->attackDamage = Short(properties.attackDamage);
    type->attackRange = Short(properties.attackRange);
    type->attackInterval = static_cast<float>(properties.attackInterval);
    type->health = properties.maxHealth;
    type->armor = properties.maxArmor;
    type->speed = Short(properties.maxSpeed);
    // Exact HW37507CF1/37507DBE and MP10969AC2/10969ACA storage contract.
    // Do not infer the runtime field from a JSON label that the loader discards.
    type->bonusMoney = Short(properties.killType);
    type->killType = 0;
    type->priority = 1;
    type->skill = 1000;
    for (std::size_t i = 0; i < type->color.size(); ++i)
        type->color[i] = static_cast<float>(settings.colors[i]);
    type->dropItems = {0};
    type->experience = 1000;
    output = std::move(type);
    return true;
}

bool SelectEliteAbilities(int difficulty, int qualifiedPlayers, EliteRandom random,
    EliteAbilities& output, ConfigError& error)
{
    error = {};
    if (!random.integer || qualifiedPlayers < 0 || qualifiedPlayers > 32)
    { error.message = "elite random selection requires its qualified player count"; return false; }
    output.clear(); // Original clears before either random callback.
    const auto roll = [&](int minimum, int maximum, int& value)
    {
        if (!random.integer(random.context, minimum, maximum, value)) return false;
        if (value < minimum || value > maximum)
        { error.message = "elite random provider returned an out-of-range value"; return false; }
        return true;
    };
    int chance{};
    if (!roll(0, 99, chance)) return false;
    int count = 1;
    int scale = 4;
    if (difficulty >= 2 && difficulty <= 7)
    {
        constexpr std::array<int, 6> minimum{2, 2, 3, 4, 5, 6};
        constexpr std::array<int, 6> maximum{3, 4, 4, 5, 6, 7};
        constexpr std::array<int, 6> scales{3, 5, 7, 9, 10, 10};
        const auto index = static_cast<std::size_t>(difficulty - 2);
        if (!roll(minimum[index], maximum[index], count)) return false;
        scale = scales[index];
    }
    if (chance < qualifiedPlayers * scale) ++count;
    for (int attempt = 0; attempt < 200 && output.size() < static_cast<std::size_t>(count); ++attempt)
    {
        int id{};
        if (!roll(0, 13, id)) return false;
        output.emplace(id, EliteAbilityClock{id});
    }
    return true;
}

bool ApplyEliteInitialModifiers(const EliteSettings& settings, int difficulty,
    const EliteAbilities& abilities, const MonsterType& original,
    std::shared_ptr<const MonsterType>& output, ConfigError& error)
{
    error = {};
    auto type = std::make_shared<MonsterType>(original);
    const auto percent = [&](int ability, const char* name, bool subtract)
    {
        // The x87 percent/add pair is rounded once when stored to float.
        const double value = static_cast<double>(settings.Parameter(ability, difficulty, name)) / 100.0;
        return static_cast<float>(subtract ? 1.0 - value : 1.0 + value);
    };
    const auto integer = [&](float value, std::int32_t& result)
    {
        const auto truncated = std::trunc(static_cast<double>(value));
        if (!std::isfinite(truncated) || truncated < INT32_MIN || truncated > INT32_MAX)
        { error.message = "elite initial modifier exceeds the finite int32 contract"; return false; }
        result = static_cast<std::int32_t>(truncated);
        return true;
    };
    std::int32_t adjusted{};
    if (abilities.contains(0))
    {
        if (!integer(static_cast<float>(type->speed) * percent(0, "Speed", false), adjusted)) return false;
        type->speed = Short(adjusted);
        type->attackInterval *= percent(0, "Attack", true);
        if (!std::isfinite(type->attackInterval))
        { error.message = "elite attack interval is not finite"; return false; }
    }
    if (abilities.contains(2))
    {
        if (!integer(static_cast<float>(type->attackDamage) * percent(2, "Percent", false), adjusted))
            return false;
        type->attackDamage = Short(adjusted);
    }
    if (abilities.contains(9))
    {
        if (!integer(static_cast<float>(type->health) * percent(9, "Percent", false), adjusted)) return false;
        type->health = adjusted;
    }
    output = std::move(type);
    return true;
}

bool StartEliteAbilityClock(const EliteSettings& settings, int difficulty,
    bool speedy, float now, EliteAbilityClock& ability, float& cooldown, ConfigError& error)
{
    error = {};
    const float duration = settings.Parameter(ability.id, difficulty, "Duration");
    cooldown = settings.Parameter(ability.id, difficulty, "CoolTime");
    if (speedy)
    {
        const float percent = settings.Parameter(0, difficulty, "Skill") / 100.0f;
        const float multiplier = 1.0f - percent;
        cooldown *= multiplier;
    }
    const float next = now + cooldown;
    const float until = now + duration;
    if (!std::isfinite(now) || !std::isfinite(cooldown) || !std::isfinite(next) || !std::isfinite(until))
    { error.message = "elite ability clock is not finite"; return false; }
    ability.nextActivation = next;
    ability.activeUntil = until;
    return true;
}

bool EliteResources::Load(ResourceFileApi files, ResourceError& error)
{
    error = {};
    constexpr const char* Path = "maps/ZBS.json";
    if (m_busy || !files.load || !files.release)
    { error = {Path, {0, "elite resource loader is unavailable or reentered"}}; return false; }
    struct Scope { bool& busy; ~Scope() { busy = false; } } scope{m_busy};
    m_busy = true;
    int length{};
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(Path, &length), files.release);
    if (!bytes || length <= 0 || length > 8 * 1024 * 1024)
    { error = {Path, {0, "elite resource is missing or has an invalid size"}}; return false; }
    auto settings = std::make_shared<EliteSettings>();
    if (!ReadEliteSettings({reinterpret_cast<const char*>(bytes.get()), static_cast<std::size_t>(length)},
            *settings, error.detail))
    { error.path = Path; return false; }
    bytes.reset();
    m_settings = std::move(settings);
    return true;
}

bool EliteResources::Clear()
{
    if (m_busy) return false;
    m_settings.reset();
    return true;
}
}
