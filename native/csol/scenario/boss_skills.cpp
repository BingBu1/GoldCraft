// SPDX-License-Identifier: GPL-3.0-or-later
#include "boss_skills.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <utility>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace csol::scenario
{
namespace
{
std::string_view LeadingSpace(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n\v\f");
    return first == value.npos ? std::string_view{} : value.substr(first);
}

std::string_view Field(const CsvRecord& record, std::size_t column)
{
    return column < record.fields.size() ? record.fields[column] : std::string_view{};
}

bool Short(std::string_view value, std::int16_t& output)
{
    value = LeadingSpace(value);
    if (value.empty()) { output = 0; return true; }
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return false;
    }
    std::int32_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{}) return false;
    output = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(parsed));
    return true;
}

bool Float(std::string_view value, float& output)
{
    // The original empty float getter does not write its destination. The SK
    // temporary record is not initialized, so reject that undefined input.
    value = LeadingSpace(value);
    if (value.empty()) return false;
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() == '+' || value.front() == '-') return false;
    }
    auto unsignedValue = value;
    if (unsignedValue.front() == '-') unsignedValue.remove_prefix(1);
    if (unsignedValue.starts_with("0x") || unsignedValue.starts_with("0X")) return false;
    double parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || !std::isfinite(parsed)) return false;
    output = static_cast<float>(parsed); // original atof double, then store float
    return std::isfinite(output);
}

float DistanceSquared(const std::array<float, 3>& from, const std::array<float, 3>& to)
{
    const float x = to[0] - from[0], y = to[1] - from[1], z = to[2] - from[2];
    const float xy = y * y + x * x;
    return xy + z * z;
}

std::int32_t AddWeight(std::int32_t sum, std::int16_t weight)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(sum) +
        static_cast<std::uint32_t>(static_cast<std::int32_t>(weight)));
}
}

bool ReadBossSkills(std::string_view bytes, BossSkillTable& output, ConfigError& error)
{
    BossSkillTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    // MP skips physical record zero and reads fixed column indices. Headers
    // and unused column zero retain their original bytes, including CP949.
    for (std::size_t index = 1; index < candidate.source.records.size(); ++index)
    {
        const auto& record = candidate.source.records[index];
        BossSkillRow row;
        row.recordIndex = index;
        if (!Short(Field(record, 1), row.id) || !Short(Field(record, 2), row.type) ||
            !Short(Field(record, 3), row.ratio) || !Short(Field(record, 18), row.startMessage))
        {
            error = {record.byteOffset, "invalid boss skill signed 16-bit field"};
            return false;
        }
        const std::pair<std::size_t, float*> floats[]{
            {4, &row.cooltime}, {5, &row.animationTime}, {7, &row.damageDelay},
            {8, &row.damage}, {9, &row.damageCosine}, {10, &row.damageHeight},
            {11, &row.damageDistance}, {12, &row.forceAttackTime},
            {13, &row.startDistanceMinSquared}, {14, &row.startDistanceMaxSquared},
            {15, &row.startHealthMin}, {16, &row.startHealthMax},
            {17, &row.approachDistanceSquared}, {19, &row.kickSide},
            {20, &row.kickUp}, {21, &row.rushPower}};
        for (const auto& [column, destination] : floats)
        {
            if (!Float(Field(record, column), *destination))
            {
                error = {record.byteOffset, "missing or invalid boss skill float column " + std::to_string(column)};
                return false;
            }
        }
        row.sequence = Field(record, 6);
        const float radians = (row.damageCosine / 180.0f) * 3.141592025756836f;
        row.damageCosine = static_cast<float>(std::cos(static_cast<double>(radians)));
        row.damageHeight *= 39.369998931884766f;
        row.damageDistance *= 39.369998931884766f;
        for (float* distance : {&row.startDistanceMinSquared, &row.startDistanceMaxSquared,
            &row.approachDistanceSquared})
        {
            const float squared = *distance * *distance;
            *distance = squared * 1549.996826171875f;
        }
        for (const auto& [column, value] : floats)
        {
            if (!std::isfinite(*value))
            {
                error = {record.byteOffset, "boss skill conversion overflow in column " + std::to_string(column)};
                return false;
            }
        }
        candidate.groups[row.id].push_back(candidate.rows.size());
        candidate.rows.push_back(std::move(row));
    }
    output = std::move(candidate);
    error = {};
    return true;
}

void BossSkillTimer::Start(float interval, float now)
{
    duration = interval;
    deadline = interval + now;
    startedAt = now;
}

BossSkills::BossSkills(BossSkillTable table) : table_(std::move(table)), states_(table_.rows.size()) {}

const BossSkillState* BossSkills::State(std::size_t row) const
{
    return row < states_.size() ? &states_[row] : nullptr;
}

bool BossSkills::Candidates(std::int16_t key, const BossSkillSubject& subject,
    std::span<const BossSkillPlayer> players, float now,
    BossSkillCandidates& output, ConfigError& error) const
{
    error = {};
    int previous{};
    for (const auto& player : players)
    {
        if (player.slot <= previous || player.slot > 32)
        { error.message = "boss skill targets must be unique ascending client slots 1..32"; return false; }
        previous = player.slot;
    }
    BossSkillCandidates candidate;
    const auto group = table_.groups.find(key);
    if (group == table_.groups.end()) { output = std::move(candidate); return true; }
    const float health = (subject.health / subject.maxHealth) * 100.0f;
    // Keep the original row x client order and repeated weight per player.
    for (const auto index : group->second)
    {
        const auto& row = table_.rows[index];
        if (!(now > states_[index].cooldownDeadline) ||
            !(health >= row.startHealthMin && health <= row.startHealthMax)) continue;
        for (const auto& player : players)
        {
            if (player.team == subject.team || player.flags == 0x80000000u || !player.alive) continue;
            const float distance = DistanceSquared(subject.origin, player.origin);
            if (!(distance >= row.startDistanceMinSquared && distance <= row.startDistanceMaxSquared)) continue;
            candidate.entries.push_back({index, player.slot});
            if (!candidate.priority)
            {
                candidate.total = AddWeight(candidate.total, row.ratio);
                if (row.ratio == 255) candidate.priority = candidate.entries.size() - 1;
            }
        }
    }
    output = std::move(candidate);
    return true;
}

bool BossSkills::Choose(const BossSkillCandidates& candidates, std::optional<std::int32_t> random,
    std::optional<BossSkillCandidate>& output, ConfigError& error) const
{
    error = {};
    if (!candidates.total) { output.reset(); return true; }
    if (candidates.priority)
    {
        if (*candidates.priority >= candidates.entries.size() ||
            candidates.entries[*candidates.priority].row >= table_.rows.size())
        { error.message = "invalid priority boss skill candidate"; return false; }
        output = candidates.entries[*candidates.priority];
        return true;
    }
    if (!random || *random < 0 || *random > 32767)
    { error.message = "weighted boss skill selection requires one UCRT rand result"; return false; }
    const auto remainder = *random % candidates.total; // original signed idiv
    std::int32_t sum{};
    for (const auto& entry : candidates.entries)
    {
        if (entry.row >= table_.rows.size())
        { error.message = "boss skill candidate is outside its table"; return false; }
        sum = AddWeight(sum, table_.rows[entry.row].ratio);
        if (remainder < sum) { output = entry; return true; }
    }
    // MP dereferences candidate[-1] here for some malformed signed weights.
    // Preserve valid signed selection, but report rather than reproduce OOB.
    error.message = "boss skill signed weights produced no selectable candidate";
    return false;
}

bool BossSkills::CommitSelection(std::size_t index, float now, BossSkillClocks& clocks)
{
    if (index >= states_.size()) return false;
    const auto& row = table_.rows[index];
    const float duration = row.forceAttackTime + row.animationTime;
    states_[index].cooldownDeadline = row.cooltime + now;
    clocks.target.Start(duration, now);
    clocks.end.Start(duration, now);
    clocks.selection.Start(duration + 0.5f, now);
    clocks.startCheck.Start(-1.0f, now);
    clocks.force.Start(row.forceAttackTime, now);
    return true;
}

bool BossSkills::CacheSequence(std::size_t row, int sequence)
{
    if (row >= states_.size()) return false;
    if (states_[row].sequence == -1)
        states_[row].sequence = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(sequence));
    return true;
}

bool BossSkills::Interrupt(std::size_t row, float now, std::int16_t hostSequenceState)
{
    if (row >= states_.size()) return false;
    if (hostSequenceState == -1) states_[row].cooldownDeadline = now;
    return true;
}

bool BossSkillResources::Load(std::string_view mapName, ResourceFileApi files,
    Current current, const void* context, ResourceError& error)
{
    error = {};
    if (loading_) { error.detail.message = "boss skill resource loader is busy"; return false; }
    struct Loading { bool& value; ~Loading() { value = false; } } scope{loading_};
    loading_ = true;
    const std::string name(mapName);
    skills_.reset();
    if (name.empty() || name.size() > 63 || !std::all_of(name.begin(), name.end(), [](unsigned char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-'; }))
    { error.detail.message = "invalid boss skill map name"; return false; }
    const auto path = "maps/zbs_sk_" + name + ".cso";
    if (!files.load || !files.release || !current)
    { error = {path, {0, "boss skill file API or session guard is unavailable"}}; return false; }
    if (!current(context)) return false;
    BossSkillTable table;
    ConfigError diagnostic;
    bool parsed{};
    {
        int length{};
        std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(path.c_str(), &length), files.release);
        if (current(context))
        {
            if (!bytes || length == 0) parsed = true; // original missing/empty file has zero records
            else if (length < 0 || length > 8 * 1024 * 1024 || length % 8)
                diagnostic.message = "invalid encrypted boss skill resource size";
            else parsed = ReadBossSkillResource(std::as_bytes(std::span(bytes.get(),
                static_cast<std::size_t>(length))), table, diagnostic);
        }
        // Keep the busy guard while freeing the engine buffer. Neither that
        // callback nor load may publish a half-read or cancelled map table.
    }
    if (!current(context)) { error = {}; return false; }
    if (!parsed) { error = {path, std::move(diagnostic)}; return false; }
    skills_ = std::make_shared<BossSkills>(std::move(table));
    error = {};
    return true;
}

bool BossSkillResources::Clear()
{
    if (loading_) return false;
    skills_.reset();
    return true;
}
}
