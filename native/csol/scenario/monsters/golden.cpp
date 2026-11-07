// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden.hpp"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace csol::scenario
{
#pragma clang fp contract(off)
namespace
{
const rapidjson::Value* Member(const rapidjson::Value& object, const char* name)
{
    if (!object.IsObject()) return nullptr;
    const auto found = object.FindMember(name);
    return found == object.MemberEnd() ? nullptr : &found->value;
}

bool Truncate(double value, std::int32_t& result)
{
    const auto integer = std::trunc(value);
    if (!std::isfinite(integer) || integer < INT32_MIN || integer > INT32_MAX) return false;
    result = static_cast<std::int32_t>(integer);
    return true;
}

bool Number(const rapidjson::Value& value, double& result)
{
    if (value.IsBool()) result = value.GetBool() ? 1.0 : 0.0;
    else if (value.IsNumber()) result = value.GetDouble();
    else return false;
    return std::isfinite(result);
}

bool FloatField(const rapidjson::Value& object, const char* name, float& result)
{
    const auto* value = Member(object, name);
    if (!value) return true;
    double number{};
    if (!Number(*value, number)) return false;
    result = static_cast<float>(number);
    return std::isfinite(result);
}

bool Integer(const rapidjson::Value& value, std::int32_t& result)
{
    double number{};
    return Number(value, number) && Truncate(number, result);
}

bool IntegerField(const rapidjson::Value& object, const char* name, std::int32_t& result)
{
    const auto* value = Member(object, name);
    return !value || Integer(*value, result);
}

std::int32_t Add(std::int32_t left, std::int32_t right)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(left) +
        static_cast<std::uint32_t>(right));
}

std::int16_t Short(int value)
{
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}
}

const GoldenDifficulty* GoldenSettings::Select(int requested) const
{
    const GoldenDifficulty* selected{};
    std::int32_t best{};
    for (const auto& row : difficulties)
        if (row.level <= requested && row.level >= best)
        { best = row.level; selected = &row; }
    return selected;
}

bool ReadGoldenSettings(std::string_view json, GoldenSettings& output, ConfigError& error)
{
    error = {};
    const auto fail = [&](const char* message) { error.message = message; return false; };
    if (json.empty() || json.size() > 8 * 1024 * 1024 || json.find('\0') != json.npos)
        return fail("invalid Golden JSON size or embedded NUL");
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    rapidjson::Document document;
    document.Parse(json.data(), json.size());
    if (document.HasParseError())
    { error = {document.GetErrorOffset(), rapidjson::GetParseError_En(document.GetParseError())}; return false; }
    const auto* root = Member(document, "ZBS_GoldenZombie");
    const auto* monster = root ? Member(*root, "monster_info") : nullptr;
    const auto* name = monster ? Member(*monster, "name") : nullptr;
    const auto* color = monster ? Member(*monster, "color") : nullptr;
    if (!monster || !monster->IsObject() || !name || !name->IsString() ||
        !color || !color->IsArray() || color->Size() < 3)
        return fail("Golden monster_info requires name and three color components");
    GoldenSettings next;
    auto& main = next.monster;
    main.name.assign(name->GetString(), name->GetStringLength());
    if (main.name.find('\0') != main.name.npos ||
        !FloatField(*monster, "probability", main.probability) ||
        !FloatField(*monster, "delay", main.delay) ||
        !IntegerField(*monster, "attackDamage", main.attackDamage) ||
        !IntegerField(*monster, "attackRange", main.attackRange) ||
        !IntegerField(*monster, "attackInterval", main.attackInterval) ||
        !IntegerField(*monster, "health", main.health) ||
        !IntegerField(*monster, "armor", main.armor) ||
        !IntegerField(*monster, "speed", main.speed) ||
        !IntegerField(*monster, "exp", main.experience) ||
        !IntegerField(*monster, "dropItemID", main.dropItemId) ||
        !IntegerField(*monster, "bonusMoney", main.bonusMoney))
        return fail("Golden monster_info contains invalid numeric or string data");
    for (rapidjson::SizeType axis = 0; axis < 3; ++axis)
        if (!Integer((*color)[axis], main.color[axis])) return fail("invalid Golden color");
    if (const auto* rows = Member(*root, "difficulty"))
    {
        if (!rows->IsArray()) return fail("Golden difficulty must be an array");
        for (const auto& value : rows->GetArray())
        {
            GoldenDifficulty row;
            float probability{};
            // HW37503DF4 narrows to float BEFORE CVTTSS2SI.
            if (!value.IsObject() || !IntegerField(value, "level", row.level) ||
                !FloatField(value, "probability", probability) || !Truncate(probability, row.probability) ||
                !IntegerField(value, "person", row.person) ||
                !IntegerField(value, "dropItemID", row.dropItemId) ||
                !IntegerField(value, "health", row.health) ||
                !IntegerField(value, "armor", row.armor) ||
                !IntegerField(value, "speed", row.speed))
                return fail("invalid Golden difficulty record");
            next.difficulties.push_back(row);
        }
    }
    output = std::move(next);
    return true;
}

bool GoldenResources::Load(ResourceFileApi files, ResourceError& error)
{
    error = {};
    constexpr const char* path = "maps/ZBS_GoldenZombie_dedi.json";
    if (loading_ || state_ != GoldenConfiguration::Unloaded || !files.load || !files.release)
    { error = {path, {0, "Golden configuration requires a fresh owner and file API"}}; return false; }
    loading_ = true;
    struct Loading { bool& value; ~Loading() { value = false; } } loading{loading_};
    int length{};
    auto* allocation = files.load(path, &length);
    if (!allocation)
    { state_ = GoldenConfiguration::Missing; return true; }
    std::string bytes;
    {
        struct Allocation
        {
            unsigned char* bytes;
            void (*release)(void*);
            ~Allocation() { release(bytes); }
        } owner{allocation, files.release};
        if (length > 0 && length <= 8 * 1024 * 1024)
            bytes.assign(reinterpret_cast<const char*>(allocation), static_cast<std::size_t>(length));
    }
    if (length < 0 || length > 8 * 1024 * 1024)
    { error = {path, {0, "Golden file API returned an invalid length"}}; return false; }
    if (bytes.empty())
    { state_ = GoldenConfiguration::Empty; return true; }
    rapidjson::Document document;
    document.Parse(bytes.data(), bytes.size());
    if (document.HasParseError())
    {
        // Original exceptions are logged and return to the caller; a fresh
        // empty owner is retained. This is not a successful nonempty parse.
        state_ = GoldenConfiguration::ParseError;
        diagnostic_ = rapidjson::GetParseError_En(document.GetParseError());
        return true;
    }
    if (document.IsNull() || (document.IsObject() && !document.HasMember("ZBS_GoldenZombie")))
    { state_ = GoldenConfiguration::Empty; return true; }
    if (document.IsObject())
    {
        const auto& root = document["ZBS_GoldenZombie"];
        // The original parser returns before any writes if monster_info is
        // absent. This class deliberately does not implement hot reload.
        if (root.IsObject() && !root.HasMember("monster_info"))
        { state_ = GoldenConfiguration::Empty; return true; }
    }
    auto settings = std::make_shared<GoldenSettings>();
    if (!ReadGoldenSettings(bytes, *settings, error.detail))
    { state_ = GoldenConfiguration::Unsupported; error.path = path; return false; }
    settings_ = std::move(settings);
    state_ = GoldenConfiguration::Configured;
    return true;
}

bool GoldenResources::EmptyPlan() const
{
    return state_ == GoldenConfiguration::Missing || state_ == GoldenConfiguration::Empty ||
        state_ == GoldenConfiguration::ParseError;
}

bool GoldenCandidateType(const MonsterType& type)
{
    return type.priority <= 0 && type.appearance != "zombieclimber" &&
        type.appearance != "flydrone" && type.appearance != "elite_forceC" &&
        type.appearance != "elite_forceT";
}

bool CollectGoldenCandidates(const MapTables& tables, int currentRound,
    std::span<const GoldenWaveStatus> statuses, std::vector<GoldenCandidate>& output,
    ConfigError& error)
{
    error = {};
    std::vector<GoldenCandidate> next;
    if (tables.rounds.rows.size() > INT32_MAX)
    { error.message = "Golden round table exceeds original signed index range"; return false; }
    const bool trigger = tables.scenarioType == 1 || tables.scenarioType == 3 ||
        tables.scenarioType == 4 || tables.scenarioType == 10;
    if (trigger && !tables.triggerRules)
    { error.message = "Golden trigger subtype requires its trigger table"; return false; }
    const auto eligible = [&](std::uint16_t wave)
    {
        const auto group = tables.waves.groups.find(wave);
        if (group == tables.waves.groups.end()) return false;
        for (const auto& [point, rows] : group->second)
            for (const auto index : rows)
            {
                if (index >= tables.waves.rows.size()) continue;
                for (const auto id : tables.waves.rows[index].types)
                    if (const auto* type = tables.monsterTypes.Find(id); type && GoldenCandidateType(*type))
                        return true;
            }
        return false;
    };
    const auto append = [&](int round, std::uint16_t wave)
    {
        if (trigger)
        {
            const auto status = std::find_if(statuses.begin(), statuses.end(),
                [&](const auto& entry) { return entry.wave == wave; });
            if (status == statuses.end())
            { error.message = "Golden trigger candidate lacks an explicit metadata/runtime snapshot"; return false; }
            if (round == currentRound && status->triggerCooldown == -1 && !status->runtimeHasPendingRow)
                return true;
        }
        if (eligible(wave)) next.push_back({round, Short(wave)});
        return true;
    };
    // Original unsigned comparison rejects a negative signed current round.
    if (currentRound >= 0)
        for (std::size_t round = static_cast<std::size_t>(currentRound); round < tables.rounds.rows.size(); ++round)
        {
            const auto& row = tables.rounds.rows[round];
            if (!trigger)
            {
                for (const auto wave : row.waves)
                    if (!append(static_cast<int>(round), WaveKey(wave))) return false;
            }
            else if (!row.waves.empty())
            {
                const auto group = tables.triggerRules->groups.find(Short(row.waves.front()));
                if (group == tables.triggerRules->groups.end()) continue;
                for (const auto index : group->second)
                {
                    if (index >= tables.triggerRules->rows.size())
                    { error.message = "Golden trigger group has an invalid row index"; return false; }
                    const auto& rule = tables.triggerRules->rows[index];
                    if (rule.command == 'w')
                        for (const auto wave : rule.waves)
                            if (!append(static_cast<int>(round), wave)) return false;
                }
            }
        }
    output = std::move(next);
    return true;
}

bool GoldenManager::Install(std::shared_ptr<const GoldenSettings> settings, int difficulty,
    GoldenExtraParameters extras, std::uint64_t session, std::uint32_t seed, ConfigError& error)
{
    error = {};
    std::int32_t persons{}, delay{};
    if (settings_ || !settings || !session || !std::isfinite(settings->monster.probability) ||
        !std::isfinite(extras.probability) || !std::isfinite(extras.secondPlanProbability) ||
        !Truncate(extras.persons, persons) || !Truncate(settings->monster.delay, delay))
    { error.message = "Golden manager requires a fresh owner and finite configuration"; return false; }
    settings_ = std::move(settings);
    difficulty_ = settings_->Select(difficulty);
    extras_ = extras;
    session_ = session;
    delay_ = delay;
    count_ = Add(difficulty_ ? difficulty_->person : 0, persons);
    random_.seed(seed);
    return true;
}

bool GoldenManager::Ready(std::span<const GoldenCandidate> candidates, ConfigError& error) const
{
    error = {};
    if (!settings_ || candidates.size() > INT32_MAX ||
        std::any_of(candidates.begin(), candidates.end(), [](const auto& row) { return row.round < 0; }))
    { error.message = "Golden plan action requires its installed owner and valid candidates"; return false; }
    return true;
}

std::uint32_t GoldenManager::Random(std::uint32_t maximum)
{
    // MP10977730 inclusive range: multiply-high with low-word rejection.
    // std::uniform_int_distribution is implementation-defined; modulo differs.
    const std::uint32_t width = maximum + 1;
    if (!width) return static_cast<std::uint32_t>(random_());
    auto product = static_cast<std::uint64_t>(random_()) * width;
    auto low = static_cast<std::uint32_t>(product);
    if (low < width)
    {
        const auto threshold = (0u - width) % width;
        while (low < threshold)
        { product = static_cast<std::uint64_t>(random_()) * width; low = static_cast<std::uint32_t>(product); }
    }
    return static_cast<std::uint32_t>(product >> 32);
}

std::optional<GoldenPlan> GoldenManager::Choose(std::span<const GoldenCandidate> candidates)
{
    if (!difficulty_ || !difficulty_->person || candidates.empty()) return {};
    const auto index = Random(static_cast<std::uint32_t>(candidates.size() - 1));
    if (!count_) return {}; // Choice still consumes RNG when adjusted count is zero.
    return GoldenPlan{candidates[index], count_};
}

std::size_t GoldenManager::FindPlan(int round, int wave) const
{
    for (std::size_t i = 0; i < plans_.size(); ++i)
        if (plans_[i].destination.round <= round && plans_[i].destination.wave == Short(wave)) return i;
    return plans_.size();
}

bool GoldenManager::Rebuild(std::span<const GoldenCandidate> candidates, ConfigError& error)
{
    if (!Ready(candidates, error)) return false;
    plans_.clear();
    if (completed_ <= 0 && difficulty_)
    {
        const float base = static_cast<float>(difficulty_->probability) + settings_->monster.probability;
        const float chance = base + extras_.probability;
        if (chance > static_cast<float>(Random(99)))
            if (auto plan = Choose(candidates)) plans_.push_back(*plan);
    }
    if ((!completed_ && plans_.empty()) || completed_ >= 2) return true;
    if (extras_.secondPlanProbability > static_cast<float>(Random(99)))
    {
        const auto attempts = Add(count_, count_);
        for (std::int32_t i = 0; i < attempts; ++i)
            if (auto plan = Choose(candidates); plan &&
                FindPlan(plan->destination.round, plan->destination.wave) == plans_.size())
            { plans_.push_back(*plan); break; }
    }
    return true;
}

bool GoldenManager::Gate(int round, int wave, float scenarioElapsed,
    std::span<const GoldenCandidate> candidates, GoldenGateResult& result, ConfigError& error)
{
    result = {};
    if (!Ready(candidates, error)) return false;
    if (!std::isfinite(scenarioElapsed))
    { error.message = "Golden scenario clock is not finite"; return false; }
    if (Short(wave) == -1) return true;
    const float elapsed = std::max(0.0f, scenarioElapsed);
    const float delay = static_cast<float>(delay_);
    if (deferred_ > 0 && elapsed > delay)
    {
        if (auto plan = Choose(candidates)) plans_.push_back(*plan);
        --deferred_; // Even an empty choice consumes one deferred request.
    }
    const auto index = FindPlan(round, wave);
    if (index == plans_.size()) return true;
    result.touchWaveRuntime = true;
    if (delay >= elapsed)
    { plans_.erase(plans_.begin() + index); deferred_ = Add(deferred_, 1); }
    else result.allowed = true;
    return true;
}

bool GoldenManager::EndWave(int round, int wave, std::span<const GoldenCandidate> candidates,
    ConfigError& error)
{
    if (!Ready(candidates, error)) return false;
    const auto index = FindPlan(round, wave);
    if (index == plans_.size()) return true;
    plans_.erase(plans_.begin() + index);
    if (auto plan = Choose(candidates)) plans_.push_back(*plan);
    return true;
}

bool GoldenManager::Convert(GoldenEntityKey key, int round, int wave,
    std::shared_ptr<const MonsterType> original, const MonsterAttributes& attributes,
    float currentHealth, std::int16_t runtimeSkill, float now, std::optional<GoldenReplacement>& result, ConfigError& error)
{
    result.reset();
    error = {};
    if (!settings_ || key.session != session_ || !key.lease || key.entity <= 0 || !original ||
        !std::isfinite(now) || !std::isfinite(currentHealth))
    { error.message = "Golden replacement requires current entity/type/clock identity"; return false; }
    const auto index = FindPlan(round, wave);
    if (Short(wave) == -1 || index == plans_.size() || !difficulty_ ||
        original->priority != 0 || currentHealth <= 0) return true;
    if (members_.contains(key.entity))
    { error.message = "Golden entity already has an unfinished member lease"; return false; }
    const auto& main = settings_->monster;
    auto clone = std::make_shared<MonsterType>(*original);
    for (std::size_t axis = 0; axis < 3; ++axis) clone->color[axis] = static_cast<float>(main.color[axis]);
    GoldenReplacement replacement;
    replacement.type = clone;
    replacement.attributes = attributes;
    replacement.runtimeSkill = runtimeSkill == 11 || runtimeSkill == 13 ? runtimeSkill : 0;
    replacement.renderUntil = static_cast<float>(static_cast<double>(now) + 999.0);
    auto& adjusted = replacement.attributes;
    const auto armor = Add(main.armor, difficulty_->armor);
    const auto health = Add(main.health, difficulty_->health);
    const auto speed = Add(main.speed, difficulty_->speed);
    replacement.overrideArmor = armor > 0;
    replacement.overrideHealth = health > 0;
    if (replacement.overrideArmor) adjusted.armor = static_cast<float>(armor);
    if (replacement.overrideHealth) adjusted.health = static_cast<float>(health);
    if (speed > 0) adjusted.speed = static_cast<float>(speed);
    if (main.attackDamage > 0) adjusted.attackDamage = static_cast<float>(main.attackDamage);
    if (main.attackRange > 0) adjusted.attackRange = static_cast<float>(main.attackRange);
    if (main.attackInterval > 0) adjusted.attackInterval = static_cast<float>(main.attackInterval);
    if (main.bonusMoney > 0) adjusted.bonusMoney = main.bonusMoney;
    members_.emplace(key.entity, Member{key, std::move(original), clone, {}, false, false});
    plans_[index].remaining = Add(plans_[index].remaining, -1);
    if (plans_[index].remaining <= 0)
    { plans_.erase(plans_.begin() + index); completed_ = Add(completed_, 1); }
    result = std::move(replacement);
    return true;
}

bool GoldenManager::Contains(GoldenEntityKey key) const
{
    const auto found = members_.find(key.entity);
    return found != members_.end() && found->second.key == key;
}

bool GoldenManager::Hit(GoldenEntityKey key, std::int32_t contributor)
{
    const auto found = members_.find(key.entity);
    if (found == members_.end() || found->second.key != key || found->second.dying) return false;
    auto& values = found->second.contributors;
    if (std::find(values.begin(), values.end(), contributor) == values.end()) values.push_back(contributor);
    return true;
}

bool GoldenManager::EarlyReclaim(GoldenEntityKey key, std::span<const GoldenCandidate> candidates,
    ConfigError& error)
{
    if (!Ready(candidates, error)) return false;
    const auto found = members_.find(key.entity);
    if (found == members_.end() || found->second.key != key || found->second.dying)
    { error.message = "Golden reclaim requires its current member lease before death"; return false; }
    if (found->second.reclaimed) return true;
    found->second.reclaimed = true;
    completed_ = Add(completed_, -1); // Original intentionally has no lower clamp.
    return Rebuild(candidates, error);
}

std::optional<GoldenDeathRecord> GoldenManager::BeginDeath(GoldenEntityKey key)
{
    const auto found = members_.find(key.entity);
    if (found == members_.end() || found->second.key != key || found->second.dying) return {};
    auto& member = found->second;
    GoldenDeathRecord result{member.original, member.contributors};
    member.dying = true;
    return result;
}

bool GoldenManager::FinishDeath(GoldenEntityKey key)
{
    const auto found = members_.find(key.entity);
    if (found == members_.end() || found->second.key != key || !found->second.dying) return false;
    members_.erase(found);
    return true;
}
}
