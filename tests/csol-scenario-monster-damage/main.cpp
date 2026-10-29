// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/monster_damage.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
using namespace csol::scenario;
constexpr std::uint32_t DamageSentinel = 0x4f123456u;
constexpr std::uint32_t ArmorSentinel = 0x4e654321u;
int checks{}, failures{};
float Float(std::uint32_t value) { return std::bit_cast<float>(value); }
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
void Check(bool value, const char* label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
void AttributeRandomCases()
{
    // Prepared from the original two-buffer and canonical integer reference;
    // see build/csol-attribute-damage-20261028/test-review/random-vectors.json.
    constexpr std::array vectors{
        std::pair{std::uint32_t{0}, std::uint64_t{0x3fe2f89545f18fe1ULL}},
        std::pair{std::uint32_t{1}, std::uint64_t{0x3fefe8f01d6d583eULL}},
        std::pair{std::uint32_t{5489}, std::uint64_t{0x3fc1574f7b6848dcULL}},
        std::pair{UINT32_MAX, std::uint64_t{0x3fbc92412218fe68ULL}}};
    for (const auto [seed, expected] : vectors)
    {
        const double value = WeaponAttributeRandom(seed);
        Check(std::bit_cast<std::uint64_t>(value) == expected, "attribute MT word order and raw double result match fixed reference");
        Check(value >= 0 && value < 1, "attribute random range is zero inclusive and one exclusive");
        (void)WeaponAttributeRandom(seed ^ 0x12345678u);
        Check(std::bit_cast<std::uint64_t>(WeaponAttributeRandom(seed)) == expected,
            "each supplied seed starts a fresh MT stream independent of intervening calls");
    }
}
void ContinuousPartRandomCases()
{
    // Fixed MT19937 seed5489 first six words. Existing attribute vectors
    // independently fix the first pair's exact double bit pattern.
    constexpr std::array<std::pair<std::uint32_t,std::uint32_t>,3> words{{
        {3499211612u,581869302u},{3890346734u,3586334585u},{545404204u,4161255391u}}};
    std::mt19937 stream(5489);
    for(const auto [first,second] : words)
    {
        const double expected=static_cast<double>((std::uint64_t{second}<<21)+(first>>11))*0x1p-53;
        Check(WeaponAttributeRandom(stream)==expected, "one part kind consumes consecutive MT word pairs without reseeding");
    }
    Check(stream()==3922919429u, "three part rolls advance exactly six MT words");
    std::mt19937 fresh(5489);
    Check(WeaponAttributeRandom(fresh)==WeaponAttributeRandom(5489u), "a new kind starts its own seeded stream");
}
void Trace(float input, int hit, float expected)
{
    float output = Float(DamageSentinel);
    Check(ResolveMonsterTraceDamage(input, hit, output), "trace accepted");
    Check(Bits(output) == Bits(expected), "trace bits");
}
void Ordinary(float input, int hit, int weapon, float expected)
{
    float output = Float(DamageSentinel);
    Check(ResolveOrdinaryMonsterDamage(input, hit, weapon, output), "ordinary accepted");
    Check(Bits(output) == Bits(expected), "ordinary bits");
}
void Armor(float input, float armor, std::uint32_t flags, float health, float remaining)
{
    MonsterArmorDamage output{Float(DamageSentinel), Float(ArmorSentinel)};
    Check(ResolveMonsterArmorDamage(input, armor, flags, output), "armor accepted");
    Check(Bits(output.healthDamage) == Bits(health), "armor health bits");
    Check(Bits(output.remainingArmor) == Bits(remaining), "armor remaining bits");
}

int Oracle()
{
    char kind{};
    while (std::cin >> kind)
    {
        std::uint32_t input{};
        if (!(std::cin >> input)) return 2;
        if (kind == 'T' || kind == 'O')
        {
            int hit{}, weapon{};
            if (!(std::cin >> hit)) return 2;
            if (kind == 'O' && !(std::cin >> weapon)) return 2;
            float output = Float(DamageSentinel);
            const bool accepted = kind == 'T' ? ResolveMonsterTraceDamage(Float(input), hit, output)
                : ResolveOrdinaryMonsterDamage(Float(input), hit, weapon, output);
            std::cout << accepted << ' ' << Bits(output) << '\n';
        }
        else if (kind == 'A')
        {
            std::uint32_t armor{}, flags{};
            if (!(std::cin >> armor >> flags)) return 2;
            MonsterArmorDamage output{Float(DamageSentinel), Float(ArmorSentinel)};
            const bool accepted = ResolveMonsterArmorDamage(Float(input), Float(armor), flags, output);
            std::cout << accepted << ' ' << Bits(output.healthDamage) << ' ' << Bits(output.remainingArmor) << '\n';
        }
        else return 2;
    }
    return std::cin.eof() ? 0 : 2;
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
    if (argc == 2 && std::string_view(argv[1]) == "--attribute-random")
    {
        AttributeRandomCases(); ContinuousPartRandomCases();
        std::cout << "checks=" << checks << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    }
    if (argc != 1) return 2;
    AttributeRandomCases(); ContinuousPartRandomCases();
    constexpr std::array traceResults{10.0f, 30.0f, 15.0f, 15.0f, 10.0f,
        10.0f, 7.5f, 7.5f, 10.0f, 10.0f};
    constexpr std::array ordinaryResults{10.0f, 30.0f, 10.0f, 12.5f, 10.0f,
        10.0f, 7.5f, 7.5f, 4.0f, 20.0f};
    for (int hit = 0; hit != 10; ++hit)
    {
        Trace(10, hit, traceResults[hit]);
        Ordinary(10, hit, 0, ordinaryResults[hit]);
    }
    for (const int hit : {std::numeric_limits<int>::min(), -1, 10, std::numeric_limits<int>::max()})
    {
        Trace(-2, hit, -2);
        Ordinary(-2, hit, 0, 0);
    }
    for (const int weapon : {18, 47, 51, 53, 71, 311, 415, 447})
    {
        Ordinary(1, 0, weapon, 345);
        Ordinary(500, 0, weapon, 500);
    }
    Ordinary(1, 0, 3, 225); Ordinary(1, 0, 40, 48); Ordinary(1, 0, 66, 330);
    Ordinary(1, 0, 52, 1); Ordinary(1, 0, 446, 1); Ordinary(1, 0, 448, 1);
    Ordinary(345, 3, 18, 431.25f); Ordinary(1, 8, 3, 90);
    // These stages deliberately differ: trace chest is x1.5, ordinary chest is x1.
    Trace(-0.0f, 0, -0.0f); Trace(-0.0f, 1, -0.0f);
    Ordinary(-0.0f, 0, 0, 0.0f); Ordinary(-10, 1, 3, 675);
    Trace(Float(1), 6, Float(1)); Trace(Float(1), 2, Float(2));
    Armor(100, 0, 0, 100, 0); Armor(100, -0.0f, 0x40, 100, -0.0f);
    Armor(100, 40, 0, 50, 15); Armor(100, 10, 0, 80, 0);
    Armor(100, 25, 0, 50, 0); Armor(100, 40, 0x40, 60, 0);
    Armor(100, 40, 0x01000000, 60, 0); Armor(100, 40, 0x20, 50, 15);
    Armor(100, 50, 0x01000040, 50, 0); Armor(0, 40, 0, 0, 40);
    Armor(-100, 40, 0, -50, 39); Armor(100, -10, 0, 120, 0);
    Armor(-100, -10, 0, -50, 0);
    for (const auto invalid : {0x7f800000u, 0xff800000u, 0x7fc12345u, 0xffc12345u})
    {
        float output = Float(DamageSentinel);
        Check(!ResolveMonsterTraceDamage(Float(invalid), 0, output), "trace invalid rejected");
        Check(Bits(output) == DamageSentinel, "trace invalid preserves output");
        Check(!ResolveOrdinaryMonsterDamage(Float(invalid), 0, 3, output), "ordinary invalid rejected");
        Check(Bits(output) == DamageSentinel, "ordinary invalid preserves output");
        for (const bool invalidArmor : {false, true})
        {
            MonsterArmorDamage candidate{Float(DamageSentinel), Float(ArmorSentinel)};
            Check(!ResolveMonsterArmorDamage(invalidArmor ? 100.0f : Float(invalid),
                invalidArmor ? Float(invalid) : 40.0f, 0, candidate), "armor invalid rejected");
            Check(Bits(candidate.healthDamage) == DamageSentinel && Bits(candidate.remainingArmor) == ArmorSentinel,
                "armor invalid preserves both outputs");
        }
    }
    float output = Float(DamageSentinel);
    Check(!ResolveMonsterTraceDamage(std::numeric_limits<float>::max(), 1, output), "trace overflow rejected");
    Check(Bits(output) == DamageSentinel, "trace overflow preserves output");
    Check(!ResolveOrdinaryMonsterDamage(std::numeric_limits<float>::max(), 9, 0, output), "ordinary overflow rejected");
    Check(Bits(output) == DamageSentinel, "ordinary overflow preserves output");
    MonsterArmorDamage candidate{Float(DamageSentinel), Float(ArmorSentinel)};
    Check(!ResolveMonsterArmorDamage(std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), 0,
        candidate), "armor overflow rejected");
    Check(Bits(candidate.healthDamage) == DamageSentinel && Bits(candidate.remainingArmor) == ArmorSentinel,
        "armor overflow preserves outputs");
    // Output may alias input storage: all inputs are by value and commit is last.
    output = 100; Check(ResolveMonsterTraceDamage(output, 1, output) && output == 300, "trace alias");
    output = 1; Check(ResolveOrdinaryMonsterDamage(output, 0, 3, output) && output == 225, "ordinary alias");
    candidate = {100, 40};
    Check(ResolveMonsterArmorDamage(candidate.healthDamage, candidate.remainingArmor, 0, candidate)
        && candidate.healthDamage == 50 && candidate.remainingArmor == 15, "armor alias");
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
