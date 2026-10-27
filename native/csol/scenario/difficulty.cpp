// SPDX-License-Identifier: GPL-3.0-or-later
#include "difficulty.hpp"
#include "../config_codec.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory>
#include <utility>

namespace csol::scenario
{
namespace
{
bool DecimalUnderflow(std::string_view token)
{
    // Only a consumed, out-of-range decimal token reaches this function.
    if (token.front() == '-') token.remove_prefix(1);
    int whole{}, zeros{}, exponent{};
    bool fraction{}, nonzero{}, negativeExponent{};
    std::size_t i{};
    for (; i < token.size() && token[i] != 'e' && token[i] != 'E'; ++i)
    {
        if (token[i] == '.') { fraction = true; continue; }
        if (!fraction) ++whole;
        if (!nonzero && token[i] == '0') ++zeros;
        else nonzero = true;
    }
    if (!nonzero) return true;
    if (i < token.size())
    {
        ++i;
        if (i < token.size() && (token[i] == '-' || token[i] == '+'))
            negativeExponent = token[i++] == '-';
        // Much larger than the 65536-byte field bound; multiply/add fits int.
        for (; i < token.size(); ++i)
            exponent = std::min(1'000'000, exponent * 10 + (token[i] - '0'));
    }
    return whole - zeros - 1 + (negativeExponent ? -exponent : exponent) < 0;
}

bool Number(std::string_view text, float& output)
{
    const auto first = text.find_first_not_of(" \t\r\n\v\f");
    output = 0.0f;
    if (first == text.npos) return true;
    text.remove_prefix(first);
    if (text.front() == '+')
    {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '+' || text.front() == '-') return true;
    }
    auto magnitude = text;
    if (magnitude.starts_with('-')) magnitude.remove_prefix(1);
    if (magnitude.starts_with("0x") || magnitude.starts_with("0X")) return false;
    double value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc::invalid_argument) return true;
    if (result.ec == std::errc::result_out_of_range)
    {
        if (!DecimalUnderflow(text.substr(0, result.ptr - text.data()))) return false;
        output = text.front() == '-' ? -0.0f : 0.0f;
        return true;
    }
    if (result.ec != std::errc{} || !std::isfinite(value)) return false;
    output = static_cast<float>(value);
    return std::isfinite(output);
}

enum class FileResult { Absent, Loaded, Invalid };

FileResult ReadFile(const std::string& path, ResourceFileApi files,
    DifficultyTable& output, ConfigError& error)
{
    int length{};
    std::unique_ptr<unsigned char, decltype(files.release)> bytes(
        files.load(path.c_str(), &length), files.release);
    if (!bytes || length == 0) return FileResult::Absent;
    if (length < 0 || length > 8 * 1024 * 1024 || length % 8)
    {
        error = {0, "invalid encrypted scenario difficulty size"};
        return FileResult::Invalid;
    }
    return ReadDifficultyResource(std::as_bytes(std::span(bytes.get(),
        static_cast<std::size_t>(length))), output, error) ? FileResult::Loaded : FileResult::Invalid;
    // The caller cannot publish output until this engine buffer is released.
}
}

bool DifficultyTable::Parameter(std::size_t row, std::size_t column,
    float& output, ConfigError& error) const
{
    error = {};
    if (row >= source.records.size())
    {
        error.message = "scenario difficulty row is missing";
        return false;
    }
    const auto& record = source.records[row];
    if (column >= record.fields.size() || record.fields[column].empty())
    {
        error = {record.byteOffset, "scenario difficulty parameter is missing"};
        return false;
    }
    float candidate{};
    if (!Number(record.fields[column], candidate))
    {
        error = {record.byteOffset, "scenario difficulty parameter exceeds the host numeric contract"};
        return false;
    }
    output = candidate;
    return true;
}

std::string_view DifficultyTable::NameToken(std::size_t row) const
{
    if (row >= source.records.size() || source.records[row].fields.size() <= 1) return {};
    return source.records[row].fields[1];
}

bool ReadDifficultyTable(std::string_view bytes, DifficultyTable& output, ConfigError& error)
{
    error = {};
    const auto fail = [&](std::size_t offset, const char* message)
    {
        error = {offset, message};
        return false;
    };
    if (bytes.empty() || bytes.size() > 8 * 1024 * 1024)
        return fail(0, "scenario difficulty table must contain 1..8388608 bytes");
    if (const auto nul = bytes.find('\0'); nul != bytes.npos)
        return fail(nul, "scenario difficulty table contains an embedded NUL");
    DifficultyTable candidate;
    candidate.source.sourceBytes.assign(bytes);
    std::size_t at{};
    while (at < bytes.size())
    {
        if (candidate.source.records.size() == 100000)
            return fail(at, "scenario difficulty table exceeds 100000 records");
        CsvRecord row{at, {}};
        bool rowEnd{};
        do
        {
            if (row.fields.size() == 256)
                return fail(at, "scenario difficulty table exceeds 256 columns");
            std::string field;
            bool columnEnd{};
            while (at < bytes.size() && !columnEnd && !rowEnd)
            {
                // The CSO string-table scanner recognizes CRLF only, retains
                // a BOM as cell text, and permits text after a quoted chunk.
                // Bare CR/LF and quotes inside an unquoted chunk are data.
                if (bytes[at] == '"')
                {
                    ++at;
                    bool closed{};
                    while (at < bytes.size())
                    {
                        const char c = bytes[at++];
                        if (c == '"')
                        {
                            if (at == bytes.size() || bytes[at] != '"') { closed = true; break; }
                            ++at;
                        }
                        if (field.size() == 65536)
                            return fail(at - 1, "scenario difficulty field exceeds 65536 bytes");
                        field.push_back(c);
                    }
                    if (!closed) return fail(at, "unterminated scenario difficulty quoted field");
                }
                else
                {
                    while (at < bytes.size())
                    {
                        const char c = bytes[at];
                        if (c == ',') { ++at; columnEnd = true; break; }
                        if (c == '\r' && at + 1 < bytes.size() && bytes[at + 1] == '\n')
                        {
                            at += 2;
                            rowEnd = true;
                            break;
                        }
                        if (field.size() == 65536)
                            return fail(at, "scenario difficulty field exceeds 65536 bytes");
                        field.push_back(c);
                        ++at;
                    }
                }
            }
            row.fields.push_back(std::move(field));
            if (!columnEnd) rowEnd = true;
            // A final comma still denotes an empty final column.
        } while (!rowEnd);
        candidate.source.records.push_back(std::move(row));
    }
    output = std::move(candidate);
    return true;
}

bool ReadDifficultyResource(std::span<const std::byte> encoded,
    DifficultyTable& output, ConfigError& error)
{
    error = {};
    if (encoded.empty() || encoded.size() > 8 * 1024 * 1024 || encoded.size() % 8)
    {
        error.message = "invalid encrypted scenario difficulty size";
        return false;
    }
    std::string plain(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    DecodeConfiguration(std::as_writable_bytes(std::span(plain.data(), plain.size())));
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    return ReadDifficultyTable(plain, output, error);
}

bool DifficultyResources::Clear()
{
    if (state_ == DifficultyState::Loading) return false;
    table_.reset();
    path_.clear();
    difficulty_ = 0;
    state_ = DifficultyState::Empty;
    return true;
}

bool DifficultyResources::BeginMap(std::string_view mapName, std::uint16_t difficulty,
    ResourceFileApi files, ResourceError& error)
{
    if (state_ == DifficultyState::Loading)
    {
        error = {{}, {0, "scenario difficulty is already loading"}};
        return false;
    }
    difficulty_ = difficulty;
    return Load(mapName, files, error);
}

bool DifficultyResources::Reload(std::string_view mapName, ResourceFileApi files, ResourceError& error)
{
    if (state_ == DifficultyState::Empty || state_ == DifficultyState::Loading)
    {
        error = {{}, {0, "scenario difficulty reload requires an initialized, idle owner"}};
        return false;
    }
    return Load(mapName, files, error);
}

bool DifficultyResources::Load(std::string_view mapName, ResourceFileApi files, ResourceError& error)
{
    // Copy before clearing: callers may pass a view into our previous table.
    const std::string map(mapName);
    table_.reset();
    path_.clear();
    state_ = DifficultyState::Failed;
    error = {};
    if (map.empty() || map.size() > 63 || !std::all_of(map.begin(), map.end(), [](unsigned char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-';
        }))
    {
        error.detail.message = "scenario difficulty requires a valid map basename";
        return false;
    }
    if (!files.load || !files.release)
    {
        error.detail.message = "scenario difficulty load and release callbacks are required";
        return false;
    }
    state_ = DifficultyState::Loading;
    struct LoadScope
    {
        DifficultyState& state;
        ~LoadScope() { if (state == DifficultyState::Loading) state = DifficultyState::Failed; }
    } operation{state_};
    DifficultyTable candidate;
    ConfigError detail;
    std::string selected = "maps/ZBS_Difficulty_" + map + ".cso";
    auto result = ReadFile(selected, files, candidate, detail);
    if (result == FileResult::Absent)
    {
        selected = "maps/ZBS_Difficulty_Default.cso";
        result = ReadFile(selected, files, candidate, detail);
    }
    if (result != FileResult::Loaded)
    {
        if (result == FileResult::Absent) detail = {0, "scenario default difficulty table is missing or empty"};
        error = {std::move(selected), std::move(detail)};
        return false;
    }
    table_ = std::move(candidate);
    path_ = std::move(selected);
    state_ = DifficultyState::Ready;
    error = {};
    return true;
}

bool DifficultyResources::Parameter(int gameMode, std::size_t column,
    float& output, ConfigError& error) const
{
    error = {};
    if ((gameMode != 15 && gameMode != 48) || state_ == DifficultyState::Empty)
    {
        output = 1.0f;
        return true;
    }
    if (state_ != DifficultyState::Ready || !table_)
    {
        error.message = "scenario difficulty table is not ready";
        return false;
    }
    return table_->Parameter(difficulty_, column, output, error);
}
}
