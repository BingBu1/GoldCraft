// SPDX-License-Identifier: GPL-3.0-or-later
// Source preparation only. This exercises the pure production calculation;
// no result-statistics owner, engine, recipient traversal or UI is substituted.
#include "../../native/csol/scenario/final_result.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, std::string_view label)
{
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}

// All expected values below are literal calculations from MP108F5A50 and
// MP108F98D0 plus the captured IEEE754 constants. No second implementation
// computes the oracle, and a result from the production function is never
// used as another call's expected value.
void EmptyAndPlayerFactors()
{
    const auto empty = SumFinalResultStatistics({});
    Check(std::bit_cast<std::uint32_t>(empty.bossDamage) == 0 &&
        std::bit_cast<std::uint32_t>(empty.ordinaryDamage) == 0 &&
        std::bit_cast<std::uint32_t>(empty.breakableDamage) == 0,
        "empty registry has three positive-zero float32 sums");
    // No damage: ordinary0 + counter10 + wall5 + boss0 + head0 + chain0 = 15.
    struct Case { std::uint32_t players; std::int32_t expected; };
    constexpr std::array cases{
        Case{0,30}, Case{1,6}, Case{3,6}, Case{4,12}, Case{6,12},
        Case{7,21}, Case{8,21}, Case{9,30}, Case{32,30}, Case{UINT32_MAX,30}};
    for (const auto& test : cases)
        Check(CalculateFinalResultScore({}, {}, test.players) == test.expected,
            "eligible-player count selects the original 0.45/0.8/1.4/2.0 float32 factor");
    Check(CalculateFinalResultScore({}, {-0.0f,-0.0f,-0.0f}, 9) == 30,
        "negative zero totals select the same zero-damage fallbacks");
}

void ComponentTruncation()
{
    // Neutral defaults: counter10 removes that bonus, wall total1 removes
    // its zero-total fallback; every other numerator is zero.
    FinalResultStatistics player{}; player.counter1C = 10;
    FinalResultTotals totals{1,1,1};
    player.ordinaryDamage = 1; totals.ordinaryDamage = 3;
    Check(CalculateFinalResultScore(player,totals,9) == 22,
        "ordinary one-third share truncates 35/3 to11 before the player factor");
    player.ordinaryDamage = 0; player.breakableDamage = 1; totals.breakableDamage = 3;
    Check(CalculateFinalResultScore(player,totals,9) == 6,
        "wall one-third share truncates 10/3 to3 before the player factor");
    player.breakableDamage = 0; player.bossDamage = 1; totals.bossDamage = 3;
    Check(CalculateFinalResultScore(player,totals,9) == 20,
        "boss one-third share contributes10 independently of ordinary and wall totals");

    player = {1,1,1,2,3,1,9,1}; totals = {3,3,3};
    // 11 ordinary + 8 counter + 3 wall + 10 boss + 1 head + 1 chain - 4 retry =30.
    Check(CalculateFinalResultScore(player,totals,9) == 60 &&
        CalculateFinalResultScore(player,totals,1) == 13,
        "each component truncates independently before integer addition and the final factor");
    player = {}; player.counter1C = 10; totals = {1,1,1};
    player.bossDamage = 2;
    Check(CalculateFinalResultScore(player,totals,9) == 60,
        "boss share alone has a signed upper cap of30");
    player.bossDamage = 0; player.ordinaryDamage = 1.5f;
    Check(CalculateFinalResultScore(player,totals,4) == 41,
        "ordinary share is not capped at35: trunc52.5 then trunc52*0.8 gives41");
    player.ordinaryDamage = 0; player.breakableDamage = 2;
    Check(CalculateFinalResultScore(player,totals,9) == 40,
        "wall share is not capped at10");
    player = {1,1,1,0,10,10,50,0}; totals = {1,1,1};
    // Full terms total100; player factor applies before the final [0,100] clamp.
    Check(CalculateFinalResultScore(player,totals,1) == 45 &&
        CalculateFinalResultScore(player,totals,4) == 80 &&
        CalculateFinalResultScore(player,totals,7) == 100,
        "complete statistics use all six terms and clamp only after the player factor");
}

void SignedCountersAndRetries()
{
    FinalResultStatistics player{};
    const FinalResultTotals noFallback{1,1,1};
    struct CounterCase { std::int32_t value, expected; };
    constexpr std::array counters{CounterCase{-1,22}, CounterCase{0,20}, CounterCase{9,2},
        CounterCase{10,0}, CounterCase{11,0}, CounterCase{INT32_MAX,0}, CounterCase{INT32_MIN,0}};
    for (const auto& test : counters)
    {
        player.counter1C = test.value;
        Check(CalculateFinalResultScore(player,noFallback,9) == test.expected,
            "counter1C uses signed min10 and modular 32-bit subtraction");
    }
    player.counter1C = -2147483637;
    // The counter term is INT32_MAX. int->float rounds to2^31; factor2 then
    // CVTTSS2SI is indefinite, while factor0.45 remains finite and clamps100.
    Check(CalculateFinalResultScore(player,noFallback,9) == 0 &&
        CalculateFinalResultScore(player,noFallback,1) == 100,
        "positive subtotal overflow after scaling keeps original CVTTSS2SI behavior");

    player = {};
    constexpr std::array retries{CounterCase{0,30}, CounterCase{1,22}, CounterCase{4,0},
        CounterCase{-1,38}, CounterCase{INT32_MAX,38}, CounterCase{INT32_MIN,30},
        CounterCase{0x40000000,30}, CounterCase{0x20000000,0}};
    for (const auto& test : retries)
    {
        player.retries = test.value;
        Check(CalculateFinalResultScore(player,{},9) == test.expected,
            "retry penalty is four times its signed field with original dword wrap before positivity clamp");
    }
}

void HeadHitsAndBestChain()
{
    FinalResultStatistics player{}; player.counter1C = 10;
    const FinalResultTotals noFallback{1,1,1};
    struct HitCase { std::int32_t hits, heads, expected; };
    constexpr std::array hits{HitCase{7,6,8}, HitCase{7,3,4}, HitCase{0,INT32_MAX,0},
        HitCase{10,20,20}, HitCase{-2,-1,4}, HitCase{2,-1,0}, HitCase{-2,1,0},
        HitCase{16777217,16777216,10}};
    for (const auto& test : hits)
    {
        player.hits = test.hits; player.headHits = test.heads;
        Check(CalculateFinalResultScore(player,noFallback,9) == test.expected,
            "head contribution divides signed head-hit and hit fields after float32 conversion, without a five-point cap");
    }
    // This is a hit ratio. Neither accumulated damage nor a monster kill
    // total is substituted for hits; the pure contract has no kill field.
    player = {};
    struct ChainCase { std::int32_t chain, expected; };
    constexpr std::array chains{ChainCase{-6,28}, ChainCase{-4,30}, ChainCase{0,30},
        ChainCase{4,30}, ChainCase{5,32}, ChainCase{49,48}, ChainCase{50,50},
        ChainCase{51,50}, ChainCase{INT32_MAX,50}, ChainCase{INT32_MIN,0}};
    for (const auto& test : chains)
    {
        player.bestChain = test.chain;
        Check(CalculateFinalResultScore(player,{},9) == test.expected,
            "bestChain below50 uses signed division by5 toward zero and50-or-more supplies10");
    }
}

void RegistryOrderAndExceptionalShares()
{
    const std::array<FinalResultStatistics,3> states{{
        {16777216.0f,16777216.0f,1.0f},
        {1.0f,-16777216.0f,16777216.0f},
        {-16777216.0f,1.0f,-16777216.0f}}};
    const auto first = SumFinalResultStatistics(states);
    Check(first.bossDamage == 0 && first.ordinaryDamage == 1 && first.breakableDamage == 0,
        "registry accumulates each damage field in its original float32 row order");
    const std::array reordered{states[0],states[2],states[1]};
    const auto second = SumFinalResultStatistics(reordered);
    Check(second.bossDamage == 1 && second.ordinaryDamage == 0 && second.breakableDamage == 1,
        "changing only registry order exposes float32 rounding instead of double or reassociated sums");

    const float nan = std::bit_cast<float>(std::uint32_t{0x7fc00001});
    const float positiveInfinity = std::bit_cast<float>(std::uint32_t{0x7f800000});
    for (const auto totals : {FinalResultTotals{nan,0,0}, FinalResultTotals{0,nan,0}, FinalResultTotals{0,0,nan}})
        Check(CalculateFinalResultScore({},totals,9) == 0,
            "unordered total takes the divide/CVTTSS2SI branch instead of the zero-total fallback");
    Check(CalculateFinalResultScore({}, {nan,nan,0}, 9) == 30,
        "two indefinite share integers wrap together before the remaining15-point subtotal is scaled");
    for (float numerator : {nan,positiveInfinity,-positiveInfinity,std::numeric_limits<float>::max()})
    {
        FinalResultStatistics player{}; player.ordinaryDamage = numerator;
        Check(CalculateFinalResultScore(player,{0,1,0},9) == 0,
            "nonfinite or weighted-overflow numerator produces x86 integer-indefinite rather than saturation");
    }
}

void WireValuesAndGrades()
{
    FinalResultStatistics player{123.875f,9876.0f,-60.0f,INT32_MIN,23,7,49,-1};
    Check(FinalResultValues(player,{456.875f,1,2}) == std::array<std::int32_t,5>{123,456,7,23,49},
        "five wire values are own boss,total boss,head hits,hits,best chain in original order");
    player.bossDamage = -2.875f;
    Check(FinalResultValues(player,{-3.875f,0,0}) == std::array<std::int32_t,5>{-2,-3,7,23,49},
        "both boss fields truncate toward zero and do not round negative fractions");
    struct FloatCase { std::uint32_t bits; std::int32_t expected; };
    constexpr std::array floats{
        FloatCase{0x4effffff,2147483520}, FloatCase{0x4f000000,INT32_MIN},
        FloatCase{0xcf000000,INT32_MIN}, FloatCase{0xcf000001,INT32_MIN},
        FloatCase{0x7fc00001,INT32_MIN}, FloatCase{0x7f800000,INT32_MIN},
        FloatCase{0xff800000,INT32_MIN}, FloatCase{0x80000000,0}};
    for (const auto& test : floats)
    {
        const auto value = std::bit_cast<float>(test.bits);
        player.bossDamage = value;
        Check(FinalResultValues(player,{value,0,0}) ==
            std::array<std::int32_t,5>{test.expected,test.expected,7,23,49},
            "wire float conversion preserves CVTTSS2SI finite limits, signed zero and indefinite results");
    }
    player.headHits = INT32_MIN; player.hits = INT32_MAX; player.bestChain = -17;
    player.bossDamage = 0;
    Check(FinalResultValues(player,{}) == std::array<std::int32_t,5>{0,0,INT32_MIN,INT32_MAX,-17},
        "head hits,hits and chain retain signed32 values without short truncation or clamping");

    struct GradeCase { std::int32_t score; std::string_view grade; };
    constexpr std::array grades{GradeCase{INT32_MIN,"D"}, GradeCase{-1,"D"}, GradeCase{0,"D"},
        GradeCase{9,"D"}, GradeCase{10,"C"}, GradeCase{24,"C"}, GradeCase{25,"B"},
        GradeCase{39,"B"}, GradeCase{40,"A"}, GradeCase{49,"A"}, GradeCase{50,"S"},
        GradeCase{100,"S"}, GradeCase{INT32_MAX,"S"}};
    for (const auto& test : grades)
        Check(FinalResultGrade(test.score) == test.grade,
            "grade uses signed comparisons at10,25,40,50 inclusive");
}
}

int main()
{
    EmptyAndPlayerFactors(); ComponentTruncation(); SignedCountersAndRetries();
    HeadHitsAndBestChain(); RegistryOrderAndExceptionalShares(); WireValuesAndGrades();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
