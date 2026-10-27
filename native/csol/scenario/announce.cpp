// SPDX-License-Identifier: GPL-3.0-or-later
#include "announce.hpp"
#include "../config_codec.hpp"

#include <bit>
#include <charconv>
#include <utility>

namespace csol::scenario
{
namespace
{
constexpr auto RibbonPath = "resource/AnnounceRibbon/announceribbon.cso";
constexpr auto CategoryPath = "resource/AnnounceRibbon/AnnounceCategory.cso";

bool Integer(std::string_view text, std::int32_t& value)
{
    // The original getter fails without a write only for an empty cell.
    // Otherwise atoi accepts a prefix, including a nonnumeric prefix as zero.
    if (text.empty()) return false;
    value = 0;
    const auto first = text.find_first_not_of(" \t\r\n\v\f");
    if (first == text.npos) return true;
    text.remove_prefix(first);
    if (text.front() == '+')
    {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9') return true;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} || parsed.ec == std::errc::invalid_argument;
}

bool Decode(ResourceFileApi files, const char* path, std::string& plain, ResourceError& error)
{
    int length{};
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(files.load(path, &length), files.release);
    if (!bytes || length <= 0 || length > 8 * 1024 * 1024 || length % 8)
    { error = {path, {0, "missing or invalid encrypted announce table"}}; return false; }
    plain.assign(reinterpret_cast<const char*>(bytes.get()), static_cast<std::size_t>(length));
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    return true;
}
}

bool AnnounceTable::Allowed(int mode, std::uint32_t id) const
{
    if (!id || id > ribbons.size()) return false;
    const auto& row = ribbons[id - 1];
    if (!row.priority || !row.category) return true;
    // CSO indexes a 61-byte array here. Reject out-of-domain modes safely.
    if (mode < 0 || mode > 60) return false;
    const auto found = categories.find(row.category);
    return found != categories.end() && found->second[mode];
}

bool ReadAnnounceTable(std::string_view ribbons, std::string_view categories,
    AnnounceTable& output, ResourceError& error)
{
    error = {};
    AnnounceTable candidate;
    if (!ReadCsv(ribbons, candidate.ribbonSource, error.detail))
    { error.path = RibbonPath; return false; }
    if (!ReadCsv(categories, candidate.categorySource, error.detail))
    { error.path = CategoryPath; return false; }
    for (std::size_t i = 1; i < candidate.ribbonSource.records.size(); ++i)
    {
        const auto& record = candidate.ribbonSource.records[i];
        const auto& fields = record.fields;
        AnnounceRibbon row;
        if (fields.size() < 6 || !Integer(fields[0], row.id) ||
            !Integer(fields[4], row.priority) || !Integer(fields[5], row.category))
        { error = {RibbonPath, {record.byteOffset, "invalid or missing announce identity/priority/category"}}; return false; }
        candidate.ribbons.push_back(row);
    }
    for (std::size_t i = 1; i < candidate.categorySource.records.size(); ++i)
    {
        const auto& record = candidate.categorySource.records[i];
        const auto& fields = record.fields;
        std::int32_t mode{};
        if (fields.empty() || !Integer(fields[0], mode))
        { error = {CategoryPath, {record.byteOffset, "invalid or missing announce mode"}}; return false; }
        if (static_cast<std::uint32_t>(mode) > 60) continue;
        for (std::size_t column = 2; column < fields.size(); ++column)
        {
            auto& enabled = candidate.categories[static_cast<std::int32_t>(column - 1)][mode];
            if (fields[column].empty()) continue; // A repeated empty cell retains the previous bit.
            std::int32_t value{};
            if (!Integer(fields[column], value))
            { error = {CategoryPath, {record.byteOffset, "announce category integer overflow"}}; return false; }
            enabled = value != 0;
        }
    }
    output = std::move(candidate);
    return true;
}

bool AnnounceResources::Load(ResourceFileApi files, ResourceError& error)
{
    error = {};
    if (loading_ || !files.load || !files.release)
    { error.detail.message = "invalid announce resource lifecycle or file API"; return false; }
    struct Loading { bool& value; ~Loading() { value = false; } } scope{loading_};
    loading_ = true;
    table_.reset();
    std::string ribbons, categories;
    // Retain one immutable snapshot only after both resources have decoded.
    if (!Decode(files, CategoryPath, categories, error) || !Decode(files, RibbonPath, ribbons, error)) return false;
    auto candidate = std::make_shared<AnnounceTable>();
    if (!ReadAnnounceTable(ribbons, categories, *candidate, error)) return false;
    table_ = std::move(candidate);
    return true;
}

bool AnnounceResources::Clear()
{
    if (loading_) return false;
    table_.reset();
    return true;
}

AnnounceCandidates AnnounceCounts::Record(int kind)
{
    if (kind == 0) ++a;
    else if (kind == 1) { ++b; ++c; }
    else if (kind == 2) ++d;
    AnnounceCandidates result;
    const auto add = [&](std::uint8_t id) { result.ids[result.count++] = id; };
    const auto sa = std::bit_cast<std::int32_t>(a), sb = std::bit_cast<std::int32_t>(b);
    if (sa > 0 && sa % 8 == 0) add(29);
    if (sb > 0 && sb % 6 == 0) add(30);
    if (!mixed && sa >= 3 && sb >= 2) { mixed = true; add(31); }
    if (c == 100) { add(39); c = 0; }
    if (d == 1) { add(42); d = 0; }
    constexpr std::array<std::uint8_t, 7> others{45, 46, 47, 50, 55, 56, 57};
    if (kind >= 3 && kind <= 9) add(others[kind - 3]);
    return result;
}
}
