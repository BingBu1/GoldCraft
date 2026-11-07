// SPDX-License-Identifier: GPL-3.0-or-later
#include "chaos_routes.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace csol::scenario
{
namespace
{
std::string_view Cell(const CsvRecord& record, std::size_t column)
{
    return column < record.fields.size() ? record.fields[column] : std::string_view{};
}

bool Fail(ConfigError& error, std::size_t offset, std::string message)
{
    error = {offset, std::move(message)};
    return false;
}

bool Integer(std::string_view value, std::int32_t& output)
{
    const auto begin = value.find_first_not_of(" \t\r\n\v\f");
    if (begin == value.npos) return false;
    value.remove_prefix(begin);
    if (value.front() == '+') value.remove_prefix(1);
    if (value.empty()) return false;
    return std::from_chars(value.data(), value.data() + value.size(), output).ec == std::errc{};
}

bool WaveIds(std::string_view value, std::vector<std::uint16_t>& output)
{
    // MP11391EF0: punctuation separates unsigned decimal runs; the original
    // temporary buffer rejects ten-digit runs. Each result is narrowed to u16.
    while (!value.empty())
    {
        const auto begin = value.find_first_of("0123456789");
        if (begin == value.npos) break;
        value.remove_prefix(begin);
        const auto end = value.find_first_not_of("0123456789");
        const auto run = value.substr(0, end);
        std::int32_t parsed{};
        if (run.size() >= 10 || !Integer(run, parsed)) return false;
        output.push_back(static_cast<std::uint16_t>(parsed));
        if (end == value.npos) break;
        value.remove_prefix(end);
    }
    return true;
}

bool RoomCount(std::size_t count, ConfigError& error)
{
    // The host exposes this index as signed16, matching the original consumers.
    return (count > 0 && count <= 32768) ||
        Fail(error, 0, "chaos requires 1..32768 physical room records");
}

std::int16_t ClampRoom(std::int32_t room, std::size_t count)
{
    return static_cast<std::int16_t>(std::clamp(room, 0, static_cast<std::int32_t>(count - 1)));
}

std::int32_t Previous(std::int32_t value)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) - 1u);
}
}

bool ReadChaosRoutes(std::string_view bytes, ChaosRouteTable& output, ConfigError& error)
{
    ChaosRouteTable candidate;
    if (!ReadCsv(bytes, candidate.source, error)) return false;
    const auto& rows = candidate.source.records;
    std::int32_t count{};
    if (rows.size() < 3 || !Integer(Cell(rows.front(), 1), count) || count <= 0 ||
        static_cast<std::size_t>(count) > rows.size())
        return Fail(error, 0, "chaos RRR route count is missing or invalid");

    ChaosRoute route;
    bool start{}, end{};
    const auto append = [&](std::size_t offset)
    {
        if (!start || !end) return Fail(error, offset, "chaos route requires explicit START and END");
        candidate.routes.push_back(std::move(route));
        route = {};
        start = end = false;
        return true;
    };

    // MP1138D683 starts at physical row2; neither header string is inspected.
    for (std::size_t i = 2; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        const auto command = Cell(row, 0);
        if (command.empty())
            return Fail(error, row.byteOffset, "chaos route command is empty before declared routes end");
        if (command == "START" || command == "END")
        {
            std::int32_t value{};
            if (!Integer(Cell(row, 1), value))
                return Fail(error, row.byteOffset, "chaos room value is missing or invalid");
            if (command == "START") { route.startRoom = value; start = true; }
            else { route.endRoom = value; end = true; }
        }
        else if (command.front() == '*')
        {
            if (!append(row.byteOffset)) return false;
            if (candidate.routes.size() == static_cast<std::size_t>(count)) break;
            ++i; // original skips the next route's physical header row
        }
        else if (command.front() == 'g')
        {
            std::int32_t value{};
            if (!Integer(Cell(row, 1), value))
                return Fail(error, row.byteOffset, "chaos exit destination is missing or invalid");
            route.exits.try_emplace(std::string(command.substr(1)), value);
        }
        else if (command.front() == 'w')
        {
            std::vector<std::uint16_t> waves, endWaves;
            if (!WaveIds(Cell(row, 1), waves) || !WaveIds(Cell(row, 2), endWaves))
                return Fail(error, row.byteOffset, "chaos wave list exceeds the original numeric scanner");
            route.waves.try_emplace(std::string(command.substr(1)), std::move(waves));
            route.endWaves.try_emplace(std::string(command.substr(1)), std::move(endWaves));
        }
        else
        {
            // Original reports an unknown row and continues without changing it.
            // The host rejects such data instead of silently losing a route rule.
            return Fail(error, row.byteOffset, "unsupported chaos route command");
        }
    }
    if (candidate.routes.size() < static_cast<std::size_t>(count) && !append(bytes.size())) return false;
    if (candidate.routes.size() != static_cast<std::size_t>(count))
        return Fail(error, bytes.size(), "chaos RRR has fewer routes than declared");
    output = std::move(candidate);
    return true;
}

bool InitializeChaosRooms(const ChaosRouteTable& routes, std::size_t roomCount,
    std::optional<ChaosRoomState> preserved, float routeOverride, std::uint32_t randomValue,
    ChaosRoomState& output, ConfigError& error)
{
    error = {};
    if (!RoomCount(roomCount, error)) return false;
    if (routes.routes.empty()) return Fail(error, 0, "chaos route table is empty");
    if (preserved)
    {
        if (preserved->route >= routes.routes.size() || preserved->room < 0 ||
            static_cast<std::size_t>(preserved->room) >= roomCount)
            return Fail(error, 0, "preserved chaos route or room is unavailable");
        output = *preserved;
        return true;
    }
    if (!std::isfinite(routeOverride) || routeOverride >= 2147483648.0f)
        return Fail(error, 0, "chaos route override is outside the original int32 conversion");
    auto selection = randomValue;
    if (routeOverride > 0)
        selection = static_cast<std::uint32_t>(static_cast<std::int32_t>(routeOverride)) - 1u;
    const auto selected = selection % routes.routes.size();
    output = {selected, ClampRoom(Previous(routes.routes[selected].startRoom), roomCount)};
    return true;
}

void ApplyChaosRoute(const ChaosRoute& route, TriggerRuleTable& triggers)
{
    for (auto& rule : triggers.rows)
    {
        if (rule.command != 'w') continue;
        const auto start = route.waves.find(rule.suffix);
        const auto end = route.endWaves.find(rule.suffix);
        rule.waves = start == route.waves.end() ? std::vector<std::uint16_t>{} : start->second;
        rule.endWaves = end == route.endWaves.end() ? std::vector<std::uint16_t>{} : end->second;
    }
}

std::vector<std::int16_t> ChaosGoalTriggers(const ChaosRoute& route, std::span<const TriggerRule> roomRules)
{
    std::vector<std::int16_t> result;
    for (const auto& rule : roomRules)
        if (rule.command == 'g' && route.exits.contains(rule.suffix)) result.push_back(rule.trigger);
    return result;
}

bool IsChaosFinalRoom(const ChaosRoute& route, std::int16_t currentRoom)
{
    return currentRoom == Previous(route.endRoom);
}

bool NextChaosRoom(const ChaosRoute& route, std::int16_t currentRoom,
    std::string_view exitSuffix, std::size_t roomCount, std::int16_t& output, ConfigError& error)
{
    error = {};
    if (!RoomCount(roomCount, error)) return false;
    if (currentRoom < 0 || static_cast<std::size_t>(currentRoom) >= roomCount)
        return Fail(error, 0, "current chaos room is unavailable");
    if (IsChaosFinalRoom(route, currentRoom)) { output = currentRoom; return true; }
    const auto found = route.exits.find(exitSuffix);
    if (found == route.exits.end()) return Fail(error, 0, "chaos goal has no selected-route destination");
    const auto next = static_cast<std::uint16_t>(static_cast<std::uint32_t>(found->second) - 1u);
    output = ClampRoom(std::bit_cast<std::int16_t>(next), roomCount);
    return true;
}
}
