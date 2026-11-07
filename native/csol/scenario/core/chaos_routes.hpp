// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "trigger_rules.hpp"

#include <optional>
#include <span>

namespace csol::scenario
{
// MP1138D260, subtype4 only. Keys omit the command's first byte, exactly like
// TriggerRule::suffix. These are room destinations, not sequential round IDs.
struct ChaosRoute
{
    std::int32_t startRoom{};
    std::int32_t endRoom{};
    std::map<std::string, std::int32_t, std::less<>> exits;
    std::map<std::string, std::vector<std::uint16_t>, std::less<>> waves;
    std::map<std::string, std::vector<std::uint16_t>, std::less<>> endWaves;
};

struct ChaosRouteTable
{
    CsvDocument source;
    std::vector<ChaosRoute> routes;
};

// Reads decoded RRR bytes. The declared route count terminates parsing at its
// delimiter; the original file has unused empty records after that point.
// Malformed/incomplete routes fail transactionally rather than reproduce the
// original uninitialized START/END or empty-name access.
bool ReadChaosRoutes(std::string_view bytes, ChaosRouteTable& output, ConfigError& error);

struct ChaosRoomState
{
    std::size_t route{};
    std::int16_t room{}; // controller1 +202, zero-based; not manager+288
    bool operator==(const ChaosRoomState&) const = default;
};

// MP1137FDBC: absent preserved state selects override/rand modulo route count
// and installs START-1. A preserved state neither selects nor resets the room.
// The owner decides which original lifecycle branch applies. This helper does
// not equate save/restore, player respawn, continuation or a new replay.
bool InitializeChaosRooms(const ChaosRouteTable& routes, std::size_t roomCount,
    std::optional<ChaosRoomState> preserved, float routeOverride, std::uint32_t randomValue,
    ChaosRoomState& output, ConfigError& error);

// MP113A2E20: replace only w start/end lists; missing keys clear those lists.
void ApplyChaosRoute(const ChaosRoute& route, TriggerRuleTable& triggers);

// MP1139D500: preserve TRG source order, selecting g names present in this route.
std::vector<std::int16_t> ChaosGoalTriggers(const ChaosRoute& route,
    std::span<const TriggerRule> roomRules);

// MP1137C380 compares the current room to full int32 END-1.
bool IsChaosFinalRoom(const ChaosRoute& route, std::int16_t currentRoom);

// MP113845B0: terminal room is unchanged; otherwise use the exit destination's
// low16 bits, subtract one, sign-extend, then the controller setter clamps it.
// Missing exits fail rather than dereference the original container sentinel.
bool NextChaosRoom(const ChaosRoute& route, std::int16_t currentRoom,
    std::string_view exitSuffix, std::size_t roomCount,
    std::int16_t& output, ConfigError& error);
}
