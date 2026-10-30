// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/server/scenario_active_skill_effects.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int checks{}, failures{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
struct Effects final : CSOLScenarioSkillEffectHost
{
    bool current{true}, playing{true}, constrained{}, retireOnStart{};
    float now{};
    mutable int clocks{};
    std::vector<int> calls;
    bool Current() const override { return current; }
    bool ReadTime(float& result, csol::scenario::ConfigError&) const override
    { ++clocks; result = now; return true; }
    bool HasActionConstraint33(bool& result, csol::scenario::ConfigError&) override
    { calls.push_back(1); result = constrained; return true; }
    bool CharacterActionPlaying(bool& result, csol::scenario::ConfigError&) override
    { calls.push_back(2); result = playing; return true; }
    bool RemoveActionConstraint33(csol::scenario::ConfigError&) override
    { calls.push_back(3); constrained = false; return true; }
    bool StartEffects(csol::scenario::ConfigError&) override
    { calls.push_back(4); if (retireOnStart) current = false; return true; }
    bool ActiveEffects(csol::scenario::ConfigError&) override
    { calls.push_back(5); return true; }
    bool EndEffects(csol::scenario::ConfigError&) override
    { calls.push_back(6); return true; }
    bool UpdateEffects(csol::scenario::ConfigError&) override
    { calls.push_back(7); return true; }
};
struct Healing final : CSOLScenarioSkillHealingHost
{
    bool current{true}, retireOnWrite{}, pulseFailure{};
    float now{}, health{50}, maximum{100};
    mutable int clocks{};
    int writes{}, pulses{};
    bool Current() const override { return current; }
    bool ReadTime(float& result, csol::scenario::ConfigError&) const override
    { ++clocks; result = now; return true; }
    bool ReadHealth(float& result, float& cap, csol::scenario::ConfigError&) const override
    { result = health; cap = maximum; return true; }
    bool WriteHealth(float value, csol::scenario::ConfigError&) override
    { ++writes; health = value; if (retireOnWrite) current = false; return true; }
    bool NotifyRecoveryPulse(csol::scenario::ConfigError& error) override
    { ++pulses; if (pulseFailure) error.message = "actual pulse sink failed"; return !pulseFailure; }
};
}

int main()
{
    using csol::scenario::ConfigError;
    {
        Effects host; host.now = 10;
        CSOLScenarioSkillEffectTimers state{12, 10, -1}; ConfigError error;
        Check(CSOL_UpdateScenarioSkillEffects(state, host, error), "pending equality accepted");
        Check(host.calls == std::vector<int>({1,2,7}), "strict deadline does not start at equality");
        Check(state.pendingAt == 10 && state.activeUntil == -1, "equal deadline preserves timers");
        host.now = 10.5f; host.calls.clear();
        Check(CSOL_UpdateScenarioSkillEffects(state, host, error), "start effects accepted");
        Check(host.calls == std::vector<int>({1,2,4,5,7}), "start and active effects run in original order");
        Check(state.pendingAt == -1 && state.activeUntil == 22.5f, "duration starts at execution time");
        host.now = 22.5f; host.calls.clear();
        Check(CSOL_UpdateScenarioSkillEffects(state, host, error), "expiry equality accepted");
        Check(host.calls == std::vector<int>({1,2,7}) && state.activeUntil == 22.5f,
            "expiry equality neither active nor ended");
        host.now = 23; host.calls.clear();
        Check(CSOL_UpdateScenarioSkillEffects(state, host, error), "expired update accepted");
        Check(host.calls == std::vector<int>({1,2,6,7}) && state.activeUntil == -1,
            "expiry ends once before unconditional update");
    }
    {
        Effects host; host.now = 20; host.playing = false; host.constrained = true;
        CSOLScenarioSkillEffectTimers state{12, 10, -1}; ConfigError error;
        Check(CSOL_UpdateScenarioSkillEffects(state, host, error), "cancelled action update accepted");
        Check(host.calls == std::vector<int>({1,2,3,2,7}), "cancelled action removes constraint without effects");
        Check(state.pendingAt == 10 && state.activeUntil == -1, "cancelled action does not invent activation");
    }
    {
        Effects host; host.now = 20; host.retireOnStart = true;
        CSOLScenarioSkillEffectTimers state{12, 10, -1}; ConfigError error;
        Check(!CSOL_UpdateScenarioSkillEffects(state, host, error), "start owner replacement cancels continuation");
        Check(state.pendingAt == 10 && state.activeUntil == -1, "retired start cannot update new owner's timers");
        Check(host.calls == std::vector<int>({1,2,4}), "retired start cannot call later virtuals");
    }
    {
        Healing host; host.now = 100; CSOLScenarioSkillHealing state; ConfigError error; bool applied{};
        Check(CSOL_InstallScenarioSkillHealing(state,12,1,.05f,host,error), "install actual healing fields");
        Check(state.expires == 112 && state.nextAt == 101 && host.clocks == 2, "installation clock ordering");
        Check(CSOL_UpdateScenarioSkillHealing(state,host,applied,error) && !applied && host.health == 50,
            "no immediate pulse at installation");
        host.now = 101;
        Check(CSOL_UpdateScenarioSkillHealing(state,host,applied,error) && applied,
            "first pulse is inclusive at next deadline");
        Check(host.health == 55 && state.nextAt == 102 && host.pulses == 1,
            "five percent of maximum and old deadline advance");
        host.now = 110;
        Check(CSOL_UpdateScenarioSkillHealing(state,host,applied,error), "delayed update succeeds");
        Check(host.health == 60 && state.nextAt == 103 && host.pulses == 2,
            "long frame processes only one pulse");
        host.now = 112;
        Check(CSOL_UpdateScenarioSkillHealing(state,host,applied,error) && !applied && host.pulses == 2,
            "expires equality suppresses pulse");
    }
    {
        Healing host; host.now = 4; host.health = 99;
        CSOLScenarioSkillHealing state{4,1,10,.05f}; ConfigError error; bool applied{};
        Check(CSOL_UpdateScenarioSkillHealing(state,host,applied,error) && applied,
            "maximum clamp pulse succeeds");
        Check(host.health == 100 && host.writes == 2 && host.pulses == 1,
            "original write then maximum clamp still notifies");
    }
    {
        Healing host; host.now = 20;
        CSOLScenarioSkillHealing state{}; ConfigError error; bool applied{true};
        Check(CSOL_UpdateScenarioSkillHealing(state,host,applied,error) && !applied,
            "empty timer is inactive");
        Check(host.clocks == 0 && host.writes == 0 && host.pulses == 0,
            "empty timer has no clock or gameplay side effects");
        state = {4,1,30,.05f}; host.retireOnWrite = true;
        Check(!CSOL_UpdateScenarioSkillHealing(state,host,applied,error), "retirement on health write cancels");
        Check(host.health == 55 && state.nextAt == 4 && host.pulses == 0,
            "completed write retained but no retired schedule or notification");
    }
    {
        Healing host; host.now = 5; host.pulseFailure = true;
        CSOLScenarioSkillHealing state{5,1,10,.05f}; ConfigError error; bool applied{};
        Check(!CSOL_UpdateScenarioSkillHealing(state,host,applied,error) && applied,
            "real notice failure is reported after actual health update");
        Check(host.health == 55 && state.nextAt == 6 && !error.message.empty(),
            "notice failure does not roll back original preceding changes");
    }
    std::printf("scenario active-skill effect checks: %d, failures: %d\n",checks,failures);
    return failures ? 1 : 0;
}
