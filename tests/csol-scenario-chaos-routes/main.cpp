// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/chaos_routes.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition) { ++failures; std::cerr << description << '\n'; }
}
std::string Read(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
bool Same(const ChaosRoute& left, const ChaosRoute& right)
{
    return left.startRoom == right.startRoom && left.endRoom == right.endRoom &&
        left.exits == right.exits && left.waves == right.waves && left.endWaves == right.endWaves;
}
bool Same(const CsvDocument& left, const CsvDocument& right)
{
    if (left.sourceBytes != right.sourceBytes || left.records.size() != right.records.size()) return false;
    for (std::size_t i = 0; i < left.records.size(); ++i)
        if (left.records[i].byteOffset != right.records[i].byteOffset ||
            left.records[i].fields != right.records[i].fields) return false;
    return true;
}

#include "expected.inc"

void Original(const std::string& rrr, const std::string& trg)
{
    ConfigError error;
    ChaosRouteTable routes;
    TriggerRuleTable triggers;
    Check(ReadChaosRoutes(rrr, routes, error), "original RRR parse");
    Check(ReadTriggerRules(trg, triggers, error), "original TRG parse");
    const auto expected = Expected();
    Check(routes.routes.size() == expected.size(), "three original routes");
    if (routes.routes.size() != expected.size()) return;
    Check(routes.source.sourceBytes == rrr, "RRR source retained byte exactly");
    Check(triggers.rows.size() == 35, "original 35 trigger rules");
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto& route = routes.routes[index];
        Check(Same(route, expected[index]), "every original exit and wave field");
        Check(route.exits.size() == 12, "original 12 exits per route");
        ChaosRoomState state{99, 99};
        Check(InitializeChaosRooms(routes, 7, {}, static_cast<float>(index + 1), 0, state, error), "original init");
        Check(state.route == index && state.room == expected[index].startRoom - 1, "original START room");
        auto applied = triggers;
        // Seed stale lists from a different route and unknown keys: reapplication
        // must replace every w row while retaining the original TRG metadata.
        for (auto& rule : applied.rows) { rule.waves = {65535}; rule.endWaves = {65534}; }
        const auto seeded = applied;
        ApplyChaosRoute(route, applied);
        Check(applied.groups == triggers.groups && Same(applied.source, triggers.source), "all groups/source unchanged");
        for (std::size_t i = 0; i < applied.rows.size(); ++i)
        {
            const auto& before = seeded.rows[i];
            const auto& after = applied.rows[i];
            Check(before.recordIndex == after.recordIndex && before.group == after.group &&
                before.command == after.command && before.suffix == after.suffix &&
                before.trigger == after.trigger && before.cooldown == after.cooldown &&
                before.destinationRatio == after.destinationRatio, "TRG metadata unchanged");
            if (after.command == 'w')
            {
                const auto ws = expected[index].waves.find(after.suffix);
                const auto es = expected[index].endWaves.find(after.suffix);
                Check(after.waves == (ws == expected[index].waves.end() ? std::vector<std::uint16_t>{} : ws->second), "actual route start waves");
                Check(after.endWaves == (es == expected[index].endWaves.end() ? std::vector<std::uint16_t>{} : es->second), "actual route end waves");
                if (after.suffix == "-c2-5s") Check(after.waves.empty() && after.endWaves.empty(), "unmapped c2-5s clears both stale lists");
            }
            else Check(after.waves == before.waves && after.endWaves == before.endWaves, "non-wave rule untouched");
        }
        for (const auto& [suffix, destination] : expected[index].exits)
        {
            std::int16_t next{-9};
            Check(NextChaosRoom(route, 0, suffix, 7, next, error) && next == destination - 1, "every original exit room");
            Check(NextChaosRoom(route, 6, suffix, 7, next, error) && next == 6, "terminal room stays");
        }
        for (int group = 1; group <= 7; ++group)
        {
            std::vector<TriggerRule> room;
            for (const auto i : triggers.groups.at(static_cast<std::int16_t>(group))) room.push_back(triggers.rows[i]);
            const auto actual = ChaosGoalTriggers(route, room);
            const std::vector<std::int16_t> goals = group == 7 ? std::vector<std::int16_t>{} :
                std::vector<std::int16_t>{static_cast<std::int16_t>(198 + group * 2), static_cast<std::int16_t>(199 + group * 2)};
            Check(actual == goals, "actual room goal order");
        }
        for (std::int16_t room = 0; room < 7; ++room)
        {
            Check(IsChaosFinalRoom(route, room) == (room == 6), "END membership");
            const ChaosRoomState preserved{index, room};
            Check(InitializeChaosRooms(routes, 7, preserved, std::numeric_limits<float>::quiet_NaN(), 0xffffffff, state, error) && state == preserved,
                "preserve branch retains route/current room and ignores selector");
            Check(InitializeChaosRooms(routes, 7, {}, 2.0f, 0, state, error) && state == ChaosRoomState{1, 0},
                "fresh branch replaces any previous route/room; not engine Replay proof");
        }
    }
}

void Instructions()
{
    ConfigError error;
    for (const auto& example : Selections)
    {
        ChaosRouteTable table;
        table.routes.resize(example.count);
        for (auto& route : table.routes) route.startRoom = 1;
        ChaosRoomState actual;
        Check(InitializeChaosRooms(table, 7, {}, example.overrideValue, example.random, actual, error) && actual.route == example.expected,
            "original selection opcodes");
    }
    for (const auto& example : Rooms)
    {
        ChaosRoute route;
        route.startRoom = example.destination;
        route.endRoom = -17;
        route.exits.emplace("-x", example.destination);
        if (example.narrowing)
        {
            std::int16_t actual{-1};
            Check(NextChaosRoom(route, 0, "-x", example.count, actual, error) && actual == example.expected, "original exit narrow/setter opcodes");
        }
        else
        {
            ChaosRouteTable table; table.routes.push_back(route);
            ChaosRoomState actual;
            Check(InitializeChaosRooms(table, example.count, {}, 1.0f, 0, actual, error) && actual.room == example.expected,
                "original START decrement/setter opcodes");
        }
    }
}

void InvalidAndOrdering()
{
    ConfigError error;
    const std::string prefix = "ignored,1\nignored,ignored\n";
    const std::string body = "START,2.9\nEND,7\ng-x,3\ng-x,6\nw-x,-1.2/65537,3 3\nw-x,9,8\n";
    ChaosRouteTable valid;
    Check(ReadChaosRoutes(prefix + body, valid, error), "valid EOF final route without delimiter");
    if (valid.routes.empty()) return;
    Check(valid.routes[0].startRoom == 2 && valid.routes[0].exits.at("-x") == 3 &&
        valid.routes[0].waves.at("-x") == std::vector<std::uint16_t>{1,2,1} &&
        valid.routes[0].endWaves.at("-x") == std::vector<std::uint16_t>{3,3}, "first duplicate wins, scanner punctuation/narrowing");
    const auto saved = valid;
    for (const std::string& data : {std::string{}, "x,0\ny,z\n" + body,
        "x,99\ny,z\n" + body, prefix + "END,7\n*\n", prefix + "START,1\n*\n",
        prefix + "START,x\nEND,7\n*\n", prefix + body + "bad,x\n",
        prefix + body + "w-bad,1234567890,\n", prefix + body + "\n*\n",
        "x,2\ny,z\n" + body + "*\nheader,\nSTART,4\n",
        "x,3\ny,z\n" + body})
    {
        Check(!ReadChaosRoutes(data, valid, error), "malformed RRR rejected");
        Check(!error.message.empty() && Same(valid.source, saved.source) && valid.routes.size() == saved.routes.size() && Same(valid.routes[0], saved.routes[0]),
            "failed RRR read preserves complete old state");
    }
    Check(ReadChaosRoutes(prefix + body + "*\n\nunknown,unused\n", valid, error), "declared count ends before trailing garbage");
    Check(ReadChaosRoutes("x,2\nheader\n" + body + "*\nTHIS HEADER IS SKIPPED\nSTART,4\nEND,7\n*\n", valid, error) &&
        valid.routes.size() == 2 && valid.routes[1].startRoom == 4, "skip exactly one next-route header");
    ChaosRoomState state{77,77};
    Check(!InitializeChaosRooms(valid, 0, {}, 1, 0, state, error) && state == ChaosRoomState{77,77}, "zero rooms transactional");
    Check(!InitializeChaosRooms(valid, 32769, {}, 1, 0, state, error) && state == ChaosRoomState{77,77}, "signed16 host room limit");
    Check(!InitializeChaosRooms(valid, 7, ChaosRoomState{2,0}, 1, 0, state, error), "bad retained route rejected");
    Check(!InitializeChaosRooms(valid, 7, ChaosRoomState{0,-1}, 1, 0, state, error), "bad retained room rejected");
    for (float overrideValue : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 2147483648.0f})
        Check(!InitializeChaosRooms(valid, 7, {}, overrideValue, 0, state, error), "undefined selector rejected");
    std::int16_t next{99};
    Check(!NextChaosRoom(valid.routes[0], 0, "-missing", 7, next, error) && next == 99, "missing exit transactional");
    Check(!NextChaosRoom(valid.routes[0], -1, "-x", 7, next, error) && next == 99, "negative current room transactional");
    Check(NextChaosRoom(valid.routes[0], 6, "-missing", 7, next, error) && next == 6, "terminal branch precedes exit lookup");
    auto largeEnd = valid.routes[0]; largeEnd.endRoom = 65543;
    Check(!IsChaosFinalRoom(largeEnd, 6), "END comparison uses full int32, not low16");
    std::vector<TriggerRule> rules(4);
    rules[0].command='g'; rules[0].suffix="-x"; rules[0].trigger=14;
    rules[1].command='g'; rules[1].suffix="-missing"; rules[1].trigger=15;
    rules[2].command='w'; rules[2].suffix="-x"; rules[2].trigger=16;
    rules[3]=rules[0]; rules[3].trigger=17;
    Check(ChaosGoalTriggers(valid.routes[0], rules) == std::vector<std::int16_t>{14,17}, "goal filter preserves source order and excludes unknown/w");
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    Original(Read(argv[1]), Read(argv[2]));
    Instructions();
    InvalidAndOrdering();
    std::cout << "chaos checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
