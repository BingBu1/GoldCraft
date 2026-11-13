// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario/custom/phase.hpp"
#include "../../native/csol/scenario/custom/stamina.hpp"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace csol::scenario::custom;
namespace
{
unsigned checks{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "custom stamina: %s\n", message); std::exit(1); }
}
void Equal(float actual, float expected, const char* message)
{ Check(std::bit_cast<unsigned>(actual) == std::bit_cast<unsigned>(expected), message); }
constexpr RoundKey Round{11, 7};
constexpr LifeKey Life{Round, 5};
StaminaInput Input(float now, std::uint64_t command, Phase phase = Phase::Night)
{ StaminaInput value; value.key = Life; value.now = now; value.command = command; value.phase = phase; return value; }
StaminaView Apply(StaminaState& state, StaminaInput input)
{ const auto value = state.Step(input); Check(bool(value), "accepted authoritative command"); return *value; }
void Phases()
{
    PhaseClock clock;
    Check(!clock.Sample(Round, 0), "unprepared clock has no phase");
    Check(clock.Begin(Round, 10), "begin custom round");
    Check(!clock.Begin(Round, 100), "duplicate preparation cannot restart round clock");
    Check(clock.Sample(Round, 189.99f)->phase == Phase::Noon, "noon lasts 180 seconds");
    Check(clock.Sample(Round, 190)->phase == Phase::Dusk, "exact 180-second boundary starts dusk");
    Equal(clock.Sample(Round, 190)->remaining, 120, "dusk receives exactly 120 seconds");
    Check(clock.Sample(Round, 309.99f)->phase == Phase::Dusk, "dusk holds until night");
    Check(clock.Sample(Round, 310)->phase == Phase::Night, "exact 300-second boundary starts night");
    Check(clock.Sample(Round, 100000)->phase == Phase::Night, "night does not cycle back to day");
    Check(!clock.Sample({12, 7}, 310) && !clock.Sample({11, 8}, 310), "foreign session or round rejected");
    Check(!clock.Sample(Round, 9) && !clock.Sample(Round, std::numeric_limits<float>::quiet_NaN()),
        "invalid time cannot manufacture a phase");
    Check(!clock.End({11, 8}) && clock.End(Round), "only current round ends clock");
    Check(clock.Sample(Round, 400)->phase == Phase::Ended, "round end terminates eternal night");
    clock.Reset(); Check(!clock.Sample(Round, 400), "map reset retires phase owner");
    Check(!clock.Begin({}, 0) && !clock.Begin(Round, 0, {0, 120}) &&
        !clock.Begin(Round, std::numeric_limits<float>::max()), "invalid duration or lost float deadline rejected");
}
void AcceptedEventsAndLives()
{
    StaminaState state;
    Check(state.Begin(Life, Phase::Night, 0), "prepare finite life");
    auto input = Input(0, 1); input.acceptedJump = true;
    Equal(Apply(state, input).amount, 85, "accepted jump debits original15 exactly once");
    Check(!state.Step(input), "same command cannot replay accepted jump");
    input = Input(0, 2);
    Equal(Apply(state, input).amount, 85, "held jump or shooting without accepted event costs nothing");
    input = Input(0, 3); input.acceptedKick = true;
    Equal(Apply(state, input).amount, 75, "accepted kick debits product default10");
    Check(state.CanKick(Life), "dusk/night availability includes actual stamina");
    Check(!state.Begin(Life, Phase::Night, 0), "weapon selection cannot refill the same life");
    auto wrong = Input(0, 4); wrong.key.life = 4;
    Check(!state.Step(wrong) && !state.View(wrong.key), "old life cannot act or observe a new life");
    input = Input(0, 4); input.dead = true;
    Check(!Apply(state, input).alive && !state.View(Life) && !state.CanKick(Life), "death retires authority");
    Check(!state.Step(Input(0, 5)) && !state.Begin(Life, Phase::Night, 0), "dead life cannot replay or rebind");
    auto next = Life; ++next.life;
    Check(state.Begin(next, Phase::Night, 0) && !state.Step(Input(0, 6)), "new life receives separate profile");
    Check(!state.Retire(Life) && state.Retire(next), "stale retirement cannot erase new life");
    auto round = Life; ++round.round.round; round.life = 1;
    Check(state.Begin(round, Phase::Noon, 0), "new round allows its own life namespace");
    Check(!state.Step(Input(0, 7)), "retired round command cannot debit next round");
}
void PhaseCapacityAndRecovery()
{
    StaminaState state;
    Check(state.Begin(Life, Phase::Noon, 0), "prepare unlimited noon life");
    auto input = Input(0, 1, Phase::Noon);
    input.wantsSprint = input.moving = input.acceptedJump = input.acceptedKick = true;
    auto value = Apply(state, input);
    Equal(value.amount, 100, "noon accepted run/jump/kick does not debit");
    Check(value.infinite && value.kickAvailable && value.sprinting, "noon infinite also permits the emergency kick");
    input = Input(180, 2, Phase::Dusk);
    value = Apply(state, input);
    Equal(value.maximum, 150, "balanced dusk capacity is150");
    Equal(value.amount, 101.25f, "dusk capacity expansion preserves amount and adds one recovery cycle");
    Check(!value.infinite && value.kickAvailable, "dusk unlocks finite kick");
    input = Input(180.25f, 3, Phase::Dusk);
    Equal(Apply(state, input).amount, 101.25f, "cycle equality does not recover");
    input = Input(180.5f, 4, Phase::Dusk); input.ducked = true;
    Equal(Apply(state, input).amount, 103.75f, "dusk duck recovery is2 times1.25");
    input = Input(300, 5, Phase::Night);
    value = Apply(state, input);
    Equal(value.maximum, 100, "night restores normal capacity");
    Equal(value.amount, 100, "night clamps dusk excess without refill");
    Check(!state.Step(Input(301, 6, Phase::Dusk)), "old phase cannot restore dusk benefits");
    StaminaState low;
    Check(low.Begin(Life, Phase::Dusk, 0), "prepare dusk life");
    input = Input(0, 1, Phase::Dusk); input.acceptedJump = true;
    Equal(Apply(low, input).amount, 135, "spawn capacity is supplied by explicit life preparation");
    for (unsigned serial = 2; serial <= 5; ++serial) { input.command = serial; Apply(low, input); }
    input = Input(0, 6, Phase::Night);
    Equal(Apply(low, input).amount, 75, "night transition does not refill depleted dusk stamina");
}
void SprintCycles()
{
    StaminaState state;
    Check(state.Begin(Life, Phase::Night, 0), "prepare sprint life");
    auto input = Input(0, 1); input.wantsSprint = input.moving = true;
    auto value = Apply(state, input);
    Equal(value.amount, 95, "sprint entry costs5 without ticking at equality");
    Equal(value.speedReference, 125, "sprint outputs original configuration reference only");
    input.now = 100; input.command = 2;
    value = Apply(state, input);
    Equal(value.amount, 93, "long frame drains one2-point cycle without catchup");
    input.now = value.nextCycle; input.command = 3;
    Equal(Apply(state, input).amount, 93, "strict drain deadline rejects equality");
    input.now = 101; input.command = 4; input.reloading = true;
    value = Apply(state, input);
    Check(!value.sprinting, "reload stops sprint");
    Equal(value.amount, 94, "stopped sprint follows original recovery amount");
    for (unsigned gate = 0; gate < 7; ++gate) {
        StaminaState blocked; Check(blocked.Begin(Life, Phase::Night, 0), "prepare gate scenario");
        input = Input(0, 1); input.wantsSprint = input.moving = true;
        if (gate == 0) input.backward = true;
        if (gate == 1) input.reloading = true;
        if (gate == 2) input.aiming = true;
        if (gate == 3) input.ladder = true;
        if (gate == 4) input.ducked = true;
        if (gate == 5) input.waterLevel = 2;
        if (gate == 6) input.moving = false;
        value = Apply(blocked, input);
        Check(!value.sprinting && value.amount == 100, "typed owner restriction prevents entry debit");
    }
    StaminaState exhausted; Check(exhausted.Begin(Life, Phase::Night, 0), "prepare exhaustion");
    input = Input(0, 1); input.acceptedJump = true;
    for (unsigned serial = 1; serial <= 7; ++serial) { input.command = serial; Apply(exhausted, input); }
    Check(!exhausted.CanKick(Life), "low stamina cannot advertise an affordable kick");
    input = Input(1, 8); value = Apply(exhausted, input);
    Equal(value.amount, 0.1f, "zero stamina seeds original0.1");
    Equal(value.nextCycle, 6, "zero stamina waits original5 seconds");
    Check(value.exhausted, "exhaustion is separate owner state");
    input = Input(6, 9); Equal(Apply(exhausted, input).amount, 0.1f, "exhaustion equality remains blocked");
    input = Input(7, 10); value = Apply(exhausted, input);
    Equal(value.amount, 1.1f, "first post-exhaustion cycle recovers original1");
    Check(!value.exhausted, "stamina at least1 clears exhaustion");
}
void PrecisionAndInvalidInputs()
{
    unsigned different{};
    for (unsigned i = 1; i <= 128; ++i) {
        const float now = std::bit_cast<float>(0x3d81ecc6u + (i - 1) * 17);
        StaminaState state; Check(state.Begin(Life, Phase::Night, 0), "prepare precision scenario");
        auto input = Input(now, 1); input.wantsSprint = input.moving = true;
        const float expected = static_cast<float>(static_cast<double>(now) + 0.1);
        const auto value = Apply(state, input);
        Equal(value.nextCycle, expected, "drain deadline uses original double interval then float store");
        different += std::bit_cast<unsigned>(expected) != std::bit_cast<unsigned>(now + .1f);
    }
    Check(different != 0, "precision samples detect a float-interval substitution");
    StaminaConfig config; config.duskRecovery = std::numeric_limits<float>::quiet_NaN();
    Check(!Valid(config), "nonfinite profile rejected cold");
    config = {}; config.drainInterval = 0; Check(!Valid(config), "zero cycle interval rejected");
    StaminaState state; Check(state.Begin(Life, Phase::Night, 5), "prepare invalid-time scenario");
    Check(!state.Step(Input(4, 1)), "time rollback rejected");
    auto input = Input(5, 1); input.now = std::numeric_limits<float>::infinity();
    Check(!state.Step(input), "nonfinite command time rejected without consuming command");
    Equal(Apply(state, Input(5, 1)).amount, 100, "rejected inputs preserve state and replay boundary");
}
}
int main()
{
    Phases(); AcceptedEventsAndLives(); PhaseCapacityAndRecovery(); SprintCycles(); PrecisionAndInvalidInputs();
    std::printf("custom phase/stamina: %u checks passed\n", checks);
}
