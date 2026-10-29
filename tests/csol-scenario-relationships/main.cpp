// SPDX-License-Identifier: GPL-3.0-or-later
// Only the actual extracted method and upstream reference method are executed.
// Classify access is controlled; these are not real SDK entities or AI tests.
#include <climits>
#include <iostream>
#include <iterator>

#define R_AL -2
#define R_FR -1
#define R_NO 0
#define R_DL 1
#define R_HT 2
#define R_NM 3
#define ARRAYSIZE(a) std::size(a)

class CBaseEntity
{
public:
    explicit CBaseEntity(int value) : classification(value) {}
    int Classify() { ++calls; return classification; }
    int classification;
    int calls{};
};
class CBaseMonster : public CBaseEntity
{
public:
    using CBaseEntity::CBaseEntity;
    int IRelationship(CBaseEntity*);
};
class ReferenceMonster : public CBaseEntity
{
public:
    using CBaseEntity::CBaseEntity;
    int IRelationship(CBaseEntity*);
};
#include "current_method.inc"
#include "reference_method.inc"

int main()
{
    int checks{}, failures{};
    const auto check = [&](bool passed, const char* what)
    {
        ++checks;
        if (!passed) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
    };
    for (int own = 0; own < 14; ++own)
        for (int other = 0; other < 14; ++other)
        {
            CBaseMonster current(own); ReferenceMonster reference(own); CBaseEntity target(other);
            const auto expected = reference.IRelationship(&target);
            target.calls = 0;
            check(current.IRelationship(&target) == expected, "all 196 legacy relation entries equal original method");
            check(current.calls == 1 && target.calls == 1, "valid classifications read exactly once");
        }
    constexpr int outside[]{INT_MIN, -99, -17, -1, 14, 15, 16, 17, 18, 99, INT_MAX};
    for (const int invalid : outside)
    {
        for (int valid = 0; valid < 14; ++valid)
        {
            CBaseMonster left(invalid), right(valid);
            CBaseEntity good(valid), bad(invalid);
            check(left.IRelationship(&good) == R_NO, "out-of-table own classification returns neutral");
            check(right.IRelationship(&bad) == R_NO, "out-of-table target classification returns neutral");
        }
        for (const int other : outside)
        {
            CBaseMonster actor(invalid); CBaseEntity target(other);
            check(actor.IRelationship(&target) == R_NO, "two invalid classifications return neutral");
        }
        CBaseMonster actor(invalid);
        check(actor.IRelationship(nullptr) == R_NO && actor.calls == 0, "null target returns before classification reads");
    }
    CBaseMonster player(2);
    check(player.IRelationship(nullptr) == R_NO && player.calls == 0, "ordinary null target guarded");
    std::cout << "scenario_relationships checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
