// SPDX-License-Identifier: GPL-3.0-or-later
#include "melee_attacks.hpp"
#include "melee_damage.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
unsigned checks{};
bool Check(bool condition, const char* name)
{
    ++checks;
    if (!condition) std::cerr << "FAIL " << checks << ": " << name << '\n';
    return condition;
}
bool Close(float a, float b) { return std::fabs(a-b) < .00001f; }
std::string Read(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
}

int main(int argc, char** argv)
{
    using namespace csol::melee;
    if (argc != 2) return 2;
    const std::string root = argv[1];
    bool pass = true;
    const auto first = BeginAttack(Variant::DragonSword, Button::Primary, 0);
    const auto second = BeginAttack(Variant::DragonSword, Button::Primary, 1);
    const auto third = BeginAttack(Variant::DragonSword, Button::Primary, 2);
    pass &= Check(first.animation == 1 && second.animation == 2 && third.animation == 1, "dragon alternating slashes");
    pass &= Check(Close(first.hitDelay, .6f) && Close(second.hitDelay, .3f), "dragon delayed callbacks differ");
    const auto firstHit = CompleteAttack(first, Contact::Damageable);
    const auto secondHit = CompleteAttack(second, Contact::Damageable);
    pass &= Check(Close(first.hitDelay + firstHit.remaining.primary, 1.2f), "dragon first hit readiness");
    pass &= Check(Close(second.hitDelay + secondHit.remaining.primary, .9f), "dragon second hit retains original fixed subtraction");
    pass &= Check(Close(second.hitDelay + CompleteAttack(second, Contact::None).remaining.primary, .95f), "dragon second miss readiness");
    pass &= Check(firstHit.dragonEffect == 0 && secondHit.dragonEffect == -1, "dragon effect emitted only by first slash callback");
    const auto stab = BeginAttack(Variant::DragonSword, Button::Secondary, 0);
    pass &= Check(stab.animation == 4 && Close(stab.hitDelay, .66f) && stab.distance == 110 && stab.fullAngle == 120, "dragon thrust geometry and timing");
    for (const auto contact : {Contact::None, Contact::World, Contact::Damageable}) {
        const auto done = CompleteAttack(stab, contact);
        pass &= Check(done.animation == 5 && done.dragonEffect == 2 && Close(stab.hitDelay + done.remaining.primary, 1.65f), "dragon stab completion for all outcomes");
    }
    const auto axe = BeginAttack(Variant::SkullAxe, Button::Primary, 0);
    pass &= Check(axe.animation == 8 && axe.distance == 95 && axe.fullAngle == 90 && axe.hitDelay == 1, "skullaxe slash_start and delayed broad strike");
    pass &= Check(CompleteAttack(axe, Contact::None).animation == 5, "skullaxe empty slash uses sequence5");
    pass &= Check(CompleteAttack(axe, Contact::World).animation == 1 && CompleteAttack(axe, Contact::Damageable).animation == 1, "skullaxe both contacts use sequence1");
    const auto axeSecondary = BeginAttack(Variant::SkullAxe, Button::Secondary, 50);
    pass &= Check(axeSecondary.animation == 2 && axeSecondary.fullAngle == 200 && axeSecondary.distance == 78, "skullaxe secondary crosses side-facing sector");
    pass &= Check(CompleteAttack(axeSecondary, Contact::Damageable).animation == -1, "skullaxe secondary does not restart view animation");

    DamageConfig dragon, skull;
    pass &= Check(ParseDamageConfig(Read(root + "/dragonsword_damage.json"), dragon), "load actual dragon JSON");
    pass &= Check(ParseDamageConfig(Read(root + "/skullaxe_damage.json"), skull), "load actual skull JSON");
    pass &= Check(Close(AttackDamage(dragon, 0, Button::Primary, false), 95) && Close(AttackDamage(dragon, 15, Button::Secondary, false), 500.5f), "dragon ordinary and scenario values");
    pass &= Check(Close(AttackDamage(skull, 8, Button::Primary, false), 2033) && Close(AttackDamage(skull, 11, Button::Primary, false), 374.5f), "skull mode11 precedes broad zombie set");
    pass &= Check(Close(AttackDamage(skull, 13, Button::Secondary, false), 374.5f) && Close(AttackDamage(skull, 15, Button::Secondary, false), 1177), "skull variant mode multipliers");
    for (const char* invalid : {"", "{}", "[]", "null", "{\"version\":1}", "{\"version\":2}"}) {
        const float oldDamage = dragon.base.primaryFull;
        pass &= Check(!ParseDamageConfig(invalid, dragon) && dragon.base.primaryFull == oldDamage, "invalid reload preserves config");
    }
    auto invalid = Read(root + "/dragonsword_damage.json");
    const auto at = invalid.find("95");
    if (at != std::string::npos) invalid.replace(at, 2, "-1");
    pass &= Check(!ParseDamageConfig(invalid, dragon) && dragon.base.primaryFull == 95, "negative damage rejected atomically");
    auto customized = Read(root + "/dragonsword_damage.json");
    const auto number = customized.find("95");
    if (number != std::string::npos) customized.replace(number, 2, "125");
    pass &= Check(ParseDamageConfig(customized, dragon) && AttackDamage(dragon, 0, Button::Primary, false) == 125 && AttackDamage(dragon, 0, Button::Primary, true) == 95, "administrator can customize full and fast damage independently");
    std::cout << checks << " checks, " << (pass ? "passed" : "failed") << '\n';
    return pass ? 0 : 1;
}
