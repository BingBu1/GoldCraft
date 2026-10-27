// SPDX-License-Identifier: GPL-3.0-or-later
#include "drop_items.hpp"
#include "map_resources.hpp"
#include "../config_codec.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <utility>

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

bool Integer(std::string_view value, std::int32_t& output)
{
    output = 0;
    value = LeadingSpace(value);
    if (value.empty()) return true;
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() < '0' || value.front() > '9') return true;
    }
    // Original getters use atoi's decimal prefix. The caller initialized
    // every output to zero before invoking even an empty/missing-cell getter.
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} || result.ec == std::errc::invalid_argument;
}

bool IsDecimalUnderflow(std::string_view token)
{
    // Called only for a decimal prefix already consumed by from_chars with
    // result_out_of_range. A magnitude below one can only have underflowed.
    if (token.front() == '-') token.remove_prefix(1);
    std::int32_t wholeDigits{};
    std::int32_t leadingZeros{};
    bool fraction{};
    bool nonzero{};
    std::size_t index{};
    for (; index < token.size() && token[index] != 'e' && token[index] != 'E'; ++index)
    {
        const char digit = token[index];
        if (digit == '.')
        {
            fraction = true;
            continue;
        }
        if (!fraction) ++wholeDigits;
        if (!nonzero && digit == '0') ++leadingZeros;
        else nonzero = true;
    }
    if (!nonzero) return true;

    std::int32_t exponent{};
    bool negativeExponent{};
    if (index < token.size())
    {
        ++index;
        if (index < token.size() && (token[index] == '-' || token[index] == '+'))
            negativeExponent = token[index++] == '-';
        // CSV fields are limited to 65536 bytes. This saturation remains far
        // beyond any mantissa adjustment while avoiding integer overflow.
        constexpr std::int32_t exponentLimit = 1'000'000;
        for (; index < token.size(); ++index)
            exponent = std::min(exponentLimit, exponent * 10 + (token[index] - '0'));
    }
    if (negativeExponent) exponent = -exponent;
    return wholeDigits - leadingZeros - 1 + exponent < 0;
}

bool Probability(std::string_view value, float& output)
{
    output = 0;
    value = LeadingSpace(value);
    if (value.empty()) return true;
    if (value.front() == '+')
    {
        value.remove_prefix(1);
        if (value.empty() || value.front() == '-' || value.front() == '+') return true;
    }
    auto unsignedValue = value;
    if (unsignedValue.front() == '-') unsignedValue.remove_prefix(1);
    // Keep the existing host's decimal-only, finite-number parsing contract.
    // These are explicit adaptations, not checks performed by the original.
    if (unsignedValue.starts_with("0x") || unsignedValue.starts_with("0X")) return false;
    double parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec == std::errc::invalid_argument) return true;
    if (result.ec == std::errc::result_out_of_range)
    {
        if (!IsDecimalUnderflow(value.substr(0, result.ptr - value.data()))) return false;
        output = value.front() == '-' ? -0.0f : 0.0f;
        return true;
    }
    if (result.ec != std::errc{} || !std::isfinite(parsed)) return false;
    // The HW getter calls atof (double), then stores a float32 result.
    output = static_cast<float>(parsed);
    return std::isfinite(output);
}
}

std::span<const DropItemRule> DropItemTable::Find(std::int16_t dropId) const
{
    const auto found = byId.find(dropId);
    return found == byId.end() ? std::span<const DropItemRule>{} : found->second;
}

bool ReadDropItemRules(std::string_view bytes, std::int32_t difficulty,
    DropItemTable& output, ConfigError& error)
{
    error = {};
    DropItemTable candidate;
    candidate.difficulty = difficulty;
    if (!bytes.empty() && !ReadCsv(bytes, candidate.source, error)) return false;
    std::map<std::int16_t, std::vector<DropItemRule>> exact;
    for (std::size_t index = 1; index < candidate.source.records.size(); ++index)
    {
        const auto& record = candidate.source.records[index];
        DropItemRule row;
        row.recordIndex = index;
        std::int32_t id{};
        if (!Integer(Field(record, 0), id) || !Integer(Field(record, 1), row.difficulty) ||
            !Integer(Field(record, 2), row.dropEffect) || !Probability(Field(record, 3), row.probability) ||
            !Integer(Field(record, 4), row.ownerType) || !Integer(Field(record, 5), row.count) ||
            !Integer(Field(record, 6), row.removeSeconds) || !Integer(Field(record, 7), row.rewardId))
        {
            error = {record.byteOffset, "scenario drop rule exceeds the host numeric parsing contract"};
            return false;
        }
        row.dropId = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(id));
        candidate.rows.push_back(row);
        if (row.difficulty == difficulty)
            exact[row.dropId].push_back(row);
        else if (row.difficulty == 0)
            candidate.byId[row.dropId].push_back(row);
    }

    for (const auto& [id, rows] : exact)
    {
        auto& selected = candidate.byId[id];
        std::unordered_set<std::int32_t> rewards;
        rewards.reserve(rows.size());
        for (const auto& row : rows) rewards.insert(row.rewardId);
        // Only rewardID overrides defaults. Remove every matching default,
        // retain the others in place, then append every exact row, duplicates
        // included. Hash iteration order across different IDs is irrelevant.
        std::erase_if(selected, [&](const auto& row) { return rewards.contains(row.rewardId); });
        selected.insert(selected.end(), rows.begin(), rows.end());
    }
    output = std::move(candidate);
    error = {};
    return true;
}

bool ReadDropItemResource(std::span<const std::byte> encoded, std::int32_t difficulty,
    DropItemTable& output, ConfigError& error)
{
    error = {};
    if (encoded.empty()) return ReadDropItemRules({}, difficulty, output, error);
    if (encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    {
        error.message = "invalid encrypted scenario drop rule size";
        return false;
    }
    std::string plain(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    const auto embedded = plain.find('\0');
    if (embedded != plain.npos)
    {
        error = {embedded, "embedded NUL in scenario drop rule"};
        return false;
    }
    return ReadDropItemRules(plain, difficulty, output, error);
}

bool DropItemResources::Clear()
{
    if (loading_) return false;
    rules_ = {};
    present_ = false;
    return true;
}

bool DropItemResources::Load(std::int32_t difficulty, ResourceFileApi files, ResourceError& error)
{
    error = {};
    constexpr const char* path = "maps/ZBS_DropItem.cso";
    if (loading_)
    {
        error = {path, {0, "scenario drop rules are already loading"}};
        return false;
    }
    Clear();
    rules_.difficulty = difficulty;
    if (!files.load || !files.release)
    {
        error = {path, {0, "scenario drop rule load and release callbacks are required"}};
        return false;
    }
    struct LoadScope
    {
        bool& loading;
        explicit LoadScope(bool& flag) : loading(flag) { loading = true; }
        ~LoadScope() { loading = false; }
    } operation(loading_);
    DropItemTable candidate;
    ConfigError detail;
    {
        int length = 0;
        std::unique_ptr<unsigned char, decltype(files.release)> bytes(
            files.load(path, &length), files.release);
        if (!bytes)
        {
            error = {};
            return true;
        }
        if (length < 0 || length > 8 * 1024 * 1024 || length % 8)
            detail.message = "invalid encrypted scenario drop rule size";
        else if (ReadDropItemResource(
            std::as_bytes(std::span(bytes.get(), static_cast<std::size_t>(length))), difficulty, candidate, detail))
        {
            // No publication or operation unlock before the release callback;
            // it may attempt to reload or clear the same resource owner.
            bytes.reset();
            rules_ = std::move(candidate);
            present_ = true;
            error = {};
            return true;
        }
    }
    error = {path, std::move(detail)};
    return false;
}
}
