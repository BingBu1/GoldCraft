// SPDX-License-Identifier: GPL-3.0-or-later
#include "spawn_points.hpp"
#include <iostream>
#include <limits>
#include <string_view>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool ok, const char* text)
{ ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << text << '\n'; } }
SpawnPoint Point(int team, int id)
{ return {{1, 2, 3}, {4, 5, 6}, team, id, 900}; }
int Oracle()
{
    int registeredTeam{}, registeredId{}, team{}, id{}, maximum{}, exists{}, repetitions{};
    std::uint32_t consumed{};
    while (std::cin >> registeredTeam >> registeredId >> team >> id >> maximum >> consumed >> exists >> repetitions)
    {
        if (repetitions < 1 || repetitions > 4 || (exists != 0 && exists != 1)) return 2;
        SpawnPoints points;
        const MonsterPoint* registered = nullptr;
        if (exists)
        {
            points.RegisterMonsterPoint(Point(registeredTeam, registeredId), maximum);
            registered = points.FindMonsterPoint(registeredTeam, registeredId);
            // Test-only boundary seed in a nonconst owner, avoiding billions of
            // calls. The method under test and key lookup are unchanged.
            const_cast<MonsterPoint*>(registered)->consumed = consumed;
        }
        for (int n = 0; n < repetitions; ++n) points.ReturnPointQuota(team, id);
        std::cout << (registered ? registered->consumed : consumed) << ' ' << points.Monsters().size() << '\n';
    }
    return std::cin.eof() ? 0 : 2;
}
void Cases()
{
    SpawnPoints points;
    points.ReturnPointQuota(1, 7);
    Check(points.Monsters().empty() && points.Players().empty(), "missing point return inserts nothing");
    points.AppendPlayerPoint(Point(1, 7)); points.ReturnPointQuota(1, 7);
    Check(points.Monsters().empty() && points.FindPlayerPoints(1, 7)->size() == 1,
        "player-only point is not a monster quota");
    points.RegisterMonsterPoint(Point(1, 7), 2);
    auto* original = points.FindMonsterPoint(1, 7);
    const auto snapshot = *original;
    points.ReturnPointQuota(1, 7);
    Check(original->consumed == 0xffffffffu, "zero decrements to uint32 maximum without clamp");
    Check(original == points.FindMonsterPoint(1, 7) && original->maxSpawn == snapshot.maxSpawn &&
        original->point.origin == snapshot.point.origin && original->point.angles == snapshot.point.angles &&
        original->point.team == snapshot.point.team && original->point.id == snapshot.point.id &&
        original->point.entityIndex == snapshot.point.entityIndex, "return only changes existing counter");
    points.ReturnPointQuota(1, 7); points.ReturnPointQuota(1, 7);
    Check(original->consumed == 0xfffffffdu, "repeated return is intentionally not idempotent");
    points.ReturnPointQuota(2, 7); points.ReturnPointQuota(1, 8);
    Check(original->consumed == 0xfffffffdu && points.Monsters().size() == 1,
        "missing distinct keys do not change an unrelated counter");
    Check(points.ConsumePointQuota(1, 7) && original->consumed == 0xfffffffeu,
        "Consume retains signed comparison after Return underflow");
    points.ResetMonsterQuotas();
    Check(points.ConsumePointQuota(1, 7) && points.ConsumePointQuota(1, 7) && !points.ConsumePointQuota(1, 7),
        "seed ordinary exhausted quota through actual Consume calls");
    points.ReturnPointQuota(1, 7);
    Check(original->consumed == 1 && points.ConsumePointQuota(1, 7) && original->consumed == 2,
        "return reopens exactly one finite attempted-spawn slot");
    for (const int maximum : {0, -1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
    {
        points.RegisterMonsterPoint(Point(1, 7), maximum);
        original = points.FindMonsterPoint(1, 7);
        points.ReturnPointQuota(1, 7);
        Check(original->consumed == 0xffffffffu && original->maxSpawn == maximum,
            "return decrements independently of unlimited or signed maxSpawn");
        if (maximum == 0)
            Check(points.ConsumePointQuota(1, 7) && original->consumed == 0xffffffffu,
                "unlimited Consume remains no-op after unconditional Return");
    }
    for (const auto count : {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu})
    {
        const_cast<MonsterPoint*>(original)->consumed = count;
        points.ReturnPointQuota(1, 7);
        Check(original->consumed == count - 1u, "return preserves exact dword subtraction across signed boundary");
    }
    points.Clear(); points.RegisterMonsterPoint(Point(1, -1), 1);
    original = points.FindMonsterPoint(1, -1);
    points.ReturnPointQuota(127, -1);
    Check(original->consumed == 0xffffffffu && points.Monsters().size() == 1,
        "negative signed16 point masks team bits in return lookup");
    points.ReturnPointQuota(-128, 65535);
    Check(original->consumed == 0xfffffffeu, "same negative point collision also crosses signed8 team boundary");
    points.Clear(); points.RegisterMonsterPoint(Point(257, 65543), 1);
    original = points.FindMonsterPoint(257, 65543);
    points.ReturnPointQuota(1, 7);
    Check(original->consumed == 0xffffffffu, "full-width team and point are narrowed before return lookup");
    points.Clear(); points.RegisterMonsterPoint(Point(-128, 1), 1);
    original = points.FindMonsterPoint(-128, 1); points.ReturnPointQuota(128, 65537);
    Check(original->consumed == 0xffffffffu, "signed8 team extension matches positive low-byte alias");
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
    Cases();
    std::cout << "quota_return checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
