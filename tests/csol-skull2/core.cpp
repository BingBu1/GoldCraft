// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/skull2.hpp"
#ifdef SKULL2_EXTENDED
#define skull2 skull2_core01
#include "legacy_core.hpp"
#undef skull2
#endif
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace sk = csol::skull2;
namespace
{
unsigned checks{}, failures{}, oracleCases{}, propertyCases{};
void Check(bool pass, const std::string& context)
{
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << context << '\n'; }
}
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
struct State
{
    int flags{}, clip{}, reserve{};
    float deadline{};
    bool operator==(const State&) const = default;
};
void CheckState(const State& value, int flags, int clip, int reserve, float deadline,
    const std::string& context)
{
    Check(value.flags == flags, context + " flags");
    Check(value.clip == clip, context + " clip");
    Check(value.reserve == reserve, context + " reserve");
    Check(Bits(value.deadline) == Bits(deadline), context + " deadline");
}
bool Commit(State& value, float now)
{
    return sk::CompleteSwitch(value.flags, value.clip, value.reserve, now, value.deadline);
}
}

int wmain(int argc, wchar_t** argv)
{
    constexpr int extraArguments =
#ifdef SKULL2_EXTENDED
        1
#else
        0
#endif
#ifdef SKULL2_HELPERS
        + 1
#endif
        ;
    if (argc != 3 + extraArguments) return 2;
    std::ifstream oracle{std::filesystem::path(argv[1])};
    std::ifstream properties{std::filesystem::path(argv[2])};
    if (!oracle || !properties) return 2;
    std::string line;
    while (std::getline(oracle, line))
    {
        std::istringstream input(line);
        char kind{}; unsigned number{};
        input >> kind >> number;
        const std::string context = "original case " + std::to_string(number);
        ++oracleCases;
        if (kind == 'C')
        {
            int flags{}, capacity{};
            input >> flags >> capacity;
            State value{flags, 11, 3, 0};
            const auto before = value;
            Check(sk::MaxClip(value.flags) == capacity, context + " capacity");
            Check(value == before, context + " capacity query is observational");
        }
        else if (kind == 'S')
        {
            int mode{}, clip{}, reserve{}, startFlags{}, equalFlags{}, equalClip{}, equalReserve{};
            int finalFlags{}, finalClip{}, finalReserve{}, sequence{};
            float deadline{};
            input >> mode >> clip >> reserve >> startFlags >> deadline >> sequence
                >> equalFlags >> equalClip >> equalReserve >> finalFlags >> finalClip >> finalReserve;
            State value{mode, clip, reserve, 0};
            const int animation = sk::BeginSwitch(value.flags, 10, value.deadline);
            Check(animation == sequence, context + " start animation");
            CheckState(value, startFlags, clip, reserve, deadline, context + " start");
            Check(!Commit(value, 13), context + " equality does not commit");
            CheckState(value, equalFlags, equalClip, equalReserve, deadline, context + " equality");
            Check(sk::MaxClip(value.flags) == 7, context + " pending queries seven");
            Check(value.clip == clip && value.reserve == reserve, context + " query preserves ammunition");

            auto cancelled = value;
            sk::CancelSwitch(cancelled.flags);
            CheckState(cancelled, mode, clip, reserve, deadline, context + " cancellation");
            Check(!Commit(cancelled, 100), context + " cancelled deadline cannot commit");
            Check(cancelled.clip == clip && cancelled.reserve == reserve, context + " cancelled ammunition unchanged");

            Check(Commit(value, 13.001f), context + " after deadline commits");
            CheckState(value, finalFlags, finalClip, finalReserve, deadline, context + " committed");
            Check(value.clip + value.reserve == clip + reserve, context + " conservation");
            const auto committed = value;
            Check(!Commit(value, 20), context + " no duplicate commit");
            Check(value == committed, context + " committed state stable");
        }
        else if (kind == 'H')
        {
            int flags{}, expected{}, clip{}, reserve{};
            input >> flags >> expected >> clip >> reserve;
            State value{flags, 7, 3, 0};
            sk::CancelSwitch(value.flags);
            CheckState(value, expected, clip, reserve, 0, context + " holster");
        }
        else if (kind == 'P')
        {
            int flags{}, expected{}; float deadline{};
            input >> flags >> expected >> deadline;
            State value{flags, 7, 3, 0};
            Check(sk::BeginSwitch(value.flags, 10, value.deadline) == -1, context + " repeat secondary omitted");
            CheckState(value, expected, 7, 3, deadline, context + " repeat secondary");
        }
        else Check(false, context + " unknown case kind");
        Check(bool(input), context + " complete input row");
    }
    Check(oracleCases == 43, "all 43 recorded original instruction cases consumed");

    // Values are read from the captured property constructor, not recreated by
    // a second copy of the production formulas.
    const std::pair<std::string_view, float> exposed[]{
        {"Distance", sk::Distance}, {"Distance_B", sk::Distance},
        {"Penetration", float(sk::Penetration)}, {"Penetration_B", float(sk::Penetration)},
        {"RangeMod", sk::RangeModifier}, {"RangeMod_B", sk::RangeModifier},
        {"MaxSpeed", sk::MaxSpeed}, {"DeployAttack", sk::DeployTime}, {"DeployIdle", sk::DeployIdle},
        {"CycleTime", sk::SingleMode.interval}, {"ReloadTime", sk::SingleMode.reloadTime},
        {"AccuracyMax", sk::SingleMode.accuracyMaximum}, {"AccuracyMin", sk::SingleMode.accuracyMinimum},
        {"AccuracyMod1", sk::SingleMode.accuracyModifier}, {"AccuracyMod2", sk::SingleMode.accuracyRecovery},
        {"SpreadJump", sk::SingleMode.jumpSpread}, {"SpreadMove", sk::SingleMode.moveSpread},
        {"SpreadDuck", sk::SingleMode.duckSpread}, {"SpreadStand", sk::SingleMode.standSpread},
        {"CycleTime_B", sk::DualMode.interval}, {"ReloadTime_B", sk::DualMode.reloadTime},
        {"AccuracyMax_B", sk::DualMode.accuracyMaximum}, {"AccuracyMin_B", sk::DualMode.accuracyMinimum},
        {"AccuracyMod1_B", sk::DualMode.accuracyModifier}, {"AccuracyMod2_B", sk::DualMode.accuracyRecovery},
        {"SpreadJump_B", sk::DualMode.jumpSpread}, {"SpreadMove_B", sk::DualMode.moveSpread},
        {"SpreadDuck_B", sk::DualMode.duckSpread}, {"SpreadStand_B", sk::DualMode.standSpread}
    };
    unsigned capturedProperties{};
    std::array<bool, 6> damageFields{};
    while (std::getline(properties, line))
    {
        std::istringstream input(line); std::string name; std::uint32_t bits{};
        input >> name >> bits; Check(bool(input), "complete property " + name);
        ++capturedProperties;
        for (const auto& [key, value] : exposed)
            if (key == name) { ++propertyCases; Check(Bits(value) == bits, "original float bits " + name); }
        for (std::size_t i = 0; i < sk::DamageFields.size(); ++i)
            if (sk::DamageFields[i] == name) damageFields[i] = true;
    }
    Check(capturedProperties == 62, "all captured property rows consumed");
    Check(propertyCases == 29, "all exposed core property values matched");
    for (bool present : damageFields) Check(present, "damage field exists in original constructor");

    // Interleave two independent entities: one pending B->A with eleven rounds,
    // the other A->B with one round. Neither query nor cancellation may touch
    // the other instance. These are lifecycle invariants, not emulator cases.
    State first{sk::Dual, 11, 35, 0}, second{sk::Single, 1, 2, 0};
    Check(sk::BeginSwitch(first.flags, 10, first.deadline) == 16, "first begins B->A");
    const auto pendingFirst = first;
    Check(sk::BeginSwitch(second.flags, 12, second.deadline) == 15, "second begins A->B");
    Check(first == pendingFirst, "second switch cannot mutate first");
    Check(sk::MaxClip(first.flags) == 7 && first.clip == 11, "pending capacity cannot trim eleven rounds");
    const auto pendingSecond = second;
    Check(Commit(first, 14), "first commits independently");
    CheckState(first, sk::Single, 7, 39, 13, "first returns surplus beyond reserve cap");
    Check(second == pendingSecond, "first commit cannot mutate second");
    sk::CancelSwitch(second.flags);
    CheckState(second, sk::Single, 1, 2, 15, "second cancellation independent");
    CheckState(first, sk::Single, 7, 39, 13, "first survives second cancellation");

    // Static IDA contracts: kept distinct from the 43 executed state cases.
    Check(sk::SingleIdle == 0 && sk::SingleReload == 1 && sk::SingleDraw == 2 && sk::SingleShoot == 3,
        "static A animation contract");
    Check(sk::DualIdle == 5 && sk::DualIdleLast == 6 && sk::LeftShoot == 7 && sk::LeftShootLast == 9 &&
        sk::RightShoot == 10 && sk::RightShootLast == 12 && sk::DualReload == 13 && sk::DualDraw == 14 &&
        sk::SingleToDual == 15 && sk::DualToSingle == 16, "static B animation contract");
    Check(sk::DrawSequence(sk::Single) == 2 && sk::DrawSequence(sk::Dual) == 14, "static draw dispatch");
    Check(sk::ReloadSequence(sk::Single) == 1 && sk::ReloadSequence(sk::Dual) == 13, "static reload dispatch");
    Check(sk::IdleSequence(sk::Dual, 1) == 6 && sk::IdleSequence(sk::Dual, 0) == 5 &&
        sk::IdleSequence(sk::Dual, 14) == 5 && sk::IdleSequence(sk::Single, 1) == 0, "static idle dispatch");
    Check(sk::ShootSequence(false, false, 0) == 3 && sk::ShootSequence(false, true, 1) == 3,
        "static A shoot never chooses empty sequence four");
    Check(sk::ShootSequence(true, true, 2) == 7 && sk::ShootSequence(true, true, 1) == 9 &&
        sk::ShootSequence(true, false, 2) == 10 && sk::ShootSequence(true, false, 0) == 12,
        "static B left and right shoot branches");
    for (int flags : {sk::Single, sk::Dual})
    {
        int shots = 0;
        Check(sk::TriggerAllowed(flags, shots) && shots == 1, "static initial trigger allowed");
        Check(!sk::TriggerAllowed(flags, shots), "static held trigger denied");
        Check(!sk::TriggerAllowed(flags, shots), "static continued held trigger denied");
        Check(shots == (flags == sk::Single ? 1 : 3), "static mode-specific over-limit count");
        shots = 0;
        Check(sk::TriggerAllowed(flags, shots), "static release resets gate");
    }
    Check(sk::Id == 312 && sk::Clip == 7 && sk::DualClip == 14 && sk::Reserve == 35 &&
        sk::AmmoBuyAmount == 7 && sk::AmmoCost == 70, "static HW catalog identity and ammunition");
    Check(sk::SingleBulletType == 27 && sk::DualBulletType == 1 && sk::VisualBulletType == 27 &&
        sk::VisualDistance == 8192, "static server and visual ballistic separation");
    Check(sk::SwitchTime == 3 && sk::IdleTime == 30 && sk::SingleMode.fireIdle == 1 &&
        sk::DualMode.fireIdle == 2, "static switch and idle timing");
#ifdef SKULL2_EXTENDED
    std::ifstream extended{std::filesystem::path(argv[3])};
    if (!extended) return 2;
    unsigned extendedCases{}, legacyBegin{}, legacyFrame{}, legacyHolster{}, displayed{};
    while (std::getline(extended, line))
    {
        std::istringstream input(line);
        char operation{}; int flags{}, clip{}, reserve{}, outFlags{}, outClip{}, outReserve{}, animation{}, committed{};
        std::uint32_t nowBits{}, deadlineBits{}, outDeadlineBits{};
        input >> operation >> flags >> clip >> reserve >> nowBits >> deadlineBits >> outFlags >> outClip
            >> outReserve >> outDeadlineBits >> animation >> committed;
        ++extendedCases;
        const auto context = "extended original " + std::to_string(extendedCases);
        State actual{flags, clip, reserve, std::bit_cast<float>(deadlineBits)};
        State legacy = actual;
        const float now = std::bit_cast<float>(nowBits);
        int actualAnimation = -1, legacyAnimation = -1;
        bool actualCommit = false, legacyCommit = false;
        if (operation == 'B')
        {
            actualAnimation = sk::BeginSwitch(actual.flags, now, actual.deadline);
            legacyAnimation = csol::skull2_core01::BeginSwitch(legacy.flags, now, legacy.deadline);
        }
        else if (operation == 'F')
        {
            actualCommit = Commit(actual, now);
            legacyCommit = csol::skull2_core01::CompleteSwitch(legacy.flags, legacy.clip, legacy.reserve, now, legacy.deadline);
        }
        else if (operation == 'H')
        {
            sk::CancelSwitch(actual.flags);
            csol::skull2_core01::CancelSwitch(legacy.flags);
        }
        else if (operation == 'N') sk::NormalizeForDeploy(actual.flags, actual.clip);
        else if (operation == 'D') sk::PrepareDrop(actual.flags, actual.clip, actual.reserve);
        else Check(false, context + " unknown operation");
        Check(bool(input), context + " complete row");
        CheckState(actual, outFlags, outClip, outReserve, std::bit_cast<float>(outDeadlineBits), context);
        Check(actualAnimation == animation, context + " animation");
        Check(actualCommit == bool(committed), context + " strict commit");
        Check(actual.clip + actual.reserve == clip + reserve, context + " ammunition conserved");
        if (operation == 'B' || operation == 'F' || operation == 'H')
        {
            const bool mismatch = legacy.flags != outFlags || legacy.clip != outClip || legacy.reserve != outReserve ||
                Bits(legacy.deadline) != outDeadlineBits || legacyAnimation != animation || legacyCommit != bool(committed);
            if (mismatch)
            {
                if (operation == 'B') ++legacyBegin;
                else if (operation == 'F') ++legacyFrame;
                else ++legacyHolster;
                if (displayed++ < 24)
                    std::cout << "counterexample op=" << operation << " flags=" << flags << " clip=" << clip
                        << " reserve=" << reserve << " old=" << legacy.flags << ',' << legacy.clip << ',' << legacy.reserve
                        << " original=" << outFlags << ',' << outClip << ',' << outReserve << '\n';
            }
        }
    }
    Check(extendedCases == 2592, "all extended original results consumed");
    Check(legacyBegin > 0 && legacyFrame > 0 && legacyHolster > 0, "old core has observed counterexamples in each corrected operation");
    std::cout << "extended cases=" << extendedCases << " legacyBegin=" << legacyBegin
        << " legacyFrame=" << legacyFrame << " legacyHolster=" << legacyHolster << '\n';
#endif
#ifdef SKULL2_HELPERS
    std::ifstream helpers{std::filesystem::path(argv[argc - 1])};
    if (!helpers) return 2;
    unsigned spreadCases{}, accuracyCases{};
    while (std::getline(helpers, line))
    {
        std::istringstream input(line);
        char operation{}; int flags{};
        input >> operation >> flags;
        const auto context = "original helper " + std::to_string(spreadCases + accuracyCases + 1);
        if (operation == 'S')
        {
            bool grounded{}, ducking{}, air{}, standing{};
            std::uint32_t speed{}, spread{}, interval{}, accuracy{};
            input >> grounded >> ducking >> speed >> air >> standing >> spread >> interval >> accuracy;
            Check(bool(input), context + " complete spread record");
            Check(Bits(sk::Spread(flags, grounded, std::bit_cast<float>(speed), ducking, air, standing)) == spread,
                context + " exact spread argument bits including special player states");
            Check(Bits(sk::Parameters(flags).interval) == interval, context + " original interval argument");
            Check(Bits(sk::Parameters(flags).accuracyMaximum) == accuracy, context + " Primary resets accuracy before Fire");
            ++spreadCases;
        }
        else if (operation == 'A')
        {
            int shots{}, expectedShots{}; bool allowed{};
            std::uint32_t previous{}, now{}, accuracy{}, lastFire{};
            input >> shots >> previous >> now >> allowed >> expectedShots >> accuracy >> lastFire;
            Check(bool(input), context + " complete accuracy record");
            Check(sk::TriggerAllowed(flags, shots) == allowed, context + " original held-trigger gate");
            Check(shots == expectedShots, context + " mode-specific trigger count");
            const float expected = allowed ? sk::AccuracyAfterAttempt(flags, std::bit_cast<float>(previous), std::bit_cast<float>(now))
                                           : sk::Parameters(flags).accuracyMaximum;
            Check(Bits(expected) == accuracy, context + " original accuracy clamp and arithmetic bits");
            Check((allowed ? now : previous) == lastFire, context + " rejected trigger retains previous-fire timestamp");
            ++accuracyCases;
        }
        else Check(false, context + " unknown helper family");
    }
    Check(spreadCases == 320 && accuracyCases == 480, "all original helper records consumed");
    // Adapter transport invariants are distinct from original MP observations.
    // The verified installed delta scales fuser3 by128. This exercises the
    // representation contract, not the engine delta encoder implementation.
    for (int shots : {-1, 0, 1, 2, 8191, 8192, 16383, 65535, 999999, 1000000, 1000001, 2147483647})
    {
        const float packed = sk::PackShots(shots);
        const int quantized = static_cast<int>(packed * 128.f);
        Check(quantized >= 0 && quantized < (1 << 21), "shot payload fits signed22 delta");
        Check(sk::UnpackShots(float(quantized) / 128.f) == std::clamp(shots, 0, 1000000),
            "shot transport keeps exact semiautomatic gate across integer quantization");
    }
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()})
        Check(sk::UnpackShots(invalid) == 0, "malformed network shot count returns neutral gate");
    Check(sk::PackFireAge(9, 10) == -1 && sk::PackFireAge(8, 10) == -1 &&
          sk::PackFireAge(10, 10) == 0 && sk::PackFireAge(11, 10) == 0,
          "fire age clamps safely within installed signed delta range");
    std::cout << "helper spread=" << spreadCases << " accuracy=" << accuracyCases << '\n';
#endif
    std::cout << "skull2-core checks=" << checks << " failures=" << failures
        << " oracleCases=" << oracleCases << " propertyValues=" << propertyCases << '\n';
    return failures ? 1 : 0;
}
