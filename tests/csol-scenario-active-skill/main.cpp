// SPDX-License-Identifier: GPL-3.0-or-later
// Directly compile the production CharacterSkill/factory with controlled host
// boundaries. This does not replace the full SDK/live death-chain regression.
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include "../../native/csol/scenario/equipment.hpp"
#include "../../native/csol/scenario_abilities.hpp"

struct Edict {};
struct CBasePlayer { Edict entity; Edict* edict() { return &entity; } };
struct EHANDLE
{
    CBasePlayer* value{};
    template<class T> T* Get() const { return static_cast<T*>(value); }
};
struct CSOLMonsterDeathCall {};
struct CSOLMonsterDamageCall
{
    CBasePlayer* player{};
    bool valid{true}, cancelled{};
    void* Current() const { return valid && !cancelled ? const_cast<CSOLMonsterDamageCall*>(this) : nullptr; }
    CBasePlayer* Player() const { return player; }
    bool Cancel() { cancelled = true; return false; }
};
// These declarations are copied verbatim by the runner from the production
// public header, so the test must implement every current virtual correctly.
#include "actual_skill_interfaces.inc"

struct CSOLScenarioPlayerRuntimeState
{
    EHANDLE player;
    std::uint64_t generation{1};
    std::uint64_t profileRevision{1}, classModRevision{1}, activeSkillRevision{};
    std::shared_ptr<CSOLScenarioActiveSkill> activeSkill;
    std::optional<csol::scenario::ClassModDefinition> activeSkillDefinition;
    int activeSkillClass{-1};
    bool activeSkillPrepared{};
};
struct Messages
{
    int calls{}, subtype{};
    float cooldown{}, duration{};
    Edict* recipient{};
    std::function<bool()> onSend;
    bool SendActiveSkillCooldown(Edict* who, int type, float cool, float active)
    {
        ++calls; subtype = type; cooldown = cool; duration = active; recipient = who;
        return onSend ? onSend() : true;
    }
};
struct Session { ::Messages messages; ::Messages& Messages() { return messages; } };
struct CCSOLScenarioPlayerRuntime : std::enable_shared_from_this<CCSOLScenarioPlayerRuntime>
{
    bool active{true};
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> registered;
    std::optional<csol::scenario::ClassModDefinition> selected;
    std::shared_ptr<Session> m_session = std::make_shared<Session>();
    bool queryValid{true};
    std::function<void()> onSet;
    int stopCalls{};
    const CSOLScenarioActiveSkill* stoppedBy{};
    std::function<bool(CSOLMonsterDamageCall&, csol::scenario::ConfigError&)> onStop;
    bool Active() const { return active; }
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> Find(EHANDLE handle) const
    { return active && registered && registered->generation && handle.value && registered->player.value == handle.value ? registered : nullptr; }
    bool StopCharacterCloaking(CSOLMonsterDamageCall& call, const CSOLScenarioActiveSkill* source,
        csol::scenario::ConfigError& error)
    {
        ++stopCalls; stoppedBy = source;
        return onStop ? onStop(call, error) : true;
    }
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> Resolve(EHANDLE handle) const { return Find(handle); }
    bool CurrentClassMod(EHANDLE handle, int type, std::optional<csol::scenario::ClassModDefinition>& output,
        csol::scenario::ConfigError& error)
    {
        if (!queryValid || type != 10 || !Find(handle)) { error.message = "selection unavailable"; return false; }
        output = selected; return true;
    }
    bool SetActiveSkill(EHANDLE handle, std::shared_ptr<CSOLScenarioActiveSkill> skill)
    {
        auto state = Resolve(handle); if (!state) return false;
        auto retired = std::exchange(state->activeSkill, std::move(skill));
        const auto revision = ++state->activeSkillRevision;
        retired.reset(); if (onSet) onSet();
        return Resolve(handle) == state && state->activeSkillRevision == revision;
    }
    bool RefreshActiveSkill(const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
        int classId, csol::scenario::ConfigError& error);
};
struct Globals { float time{}; } globals;
Globals* gpGlobals = &globals;
#include "actual_character_skill.inc"
#include "actual_refresh_skill.inc"

int checks{};
int RunSenderTests();
int RunResourceTests();
int RunRuntimeHudTests();
void Check(bool value, const char* text)
{
    ++checks;
    if (!value) { std::cerr << "FAIL " << checks << ": " << text << '\n'; std::exit(1); }
}
struct Fixture
{
    CBasePlayer player;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> owner = std::make_shared<CCSOLScenarioPlayerRuntime>();
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> state = std::make_shared<CSOLScenarioPlayerRuntimeState>();
    csol::scenario::ClassModDefinition definition{};
    csol::scenario::ConfigError error;
    Fixture(int type)
    {
        state->player.value = &player; owner->registered = state;
        definition.type = 10; definition.subtype = static_cast<std::int16_t>(type);
        definition.values[0] = 50; definition.values[1] = 12;
        gpGlobals = &globals; globals.time = 10;
    }
    std::shared_ptr<CSOLScenarioActiveSkill> Make()
    { return CSOL_CreateScenarioActiveSkill(owner, state, definition, globals.time, error); }
};
int main()
{
    for (int type : {1,29})
    {
        Fixture f(type); auto skill=f.Make(); csol::abilities::Snapshot snapshot;
        Check(skill->HudSnapshot(30,snapshot) && snapshot.type==unsigned(type) && snapshot.cooldown==50 &&
            snapshot.elapsed==20 && snapshot.duration==0, "HUD snapshot retains original cooldown and elapsed time");
        Check(skill->HudSnapshot(40,snapshot) && snapshot.elapsed==30,
            "repeated snapshot advances original timer without resetting it");
        Check(skill->HudSnapshot(60,snapshot) && snapshot.elapsed==50,
            "ready snapshot retains full cooldown instead of hiding the icon");
        Check(skill->HudSnapshot(100,snapshot) && snapshot.elapsed==50,
            "late snapshot clamps elapsed to completed cooldown");
        Check(skill->HudSnapshot(5,snapshot) && snapshot.elapsed==0,
            "clock before construction clamps elapsed to zero");
        Check(!skill->HudSnapshot(std::numeric_limits<float>::quiet_NaN(),snapshot), "nonfinite snapshot clock rejected");
        ++f.state->generation;
        Check(!skill->HudSnapshot(30,snapshot), "retired generation cannot supply a snapshot");
    }
    for (int failure=0;failure<4;++failure)
    {
        Fixture f(29); auto skill=f.Make(); csol::abilities::Snapshot snapshot;
        if(failure==0)f.owner->active=false;
        if(failure==1)f.owner->registered.reset();
        if(failure==2)f.owner.reset();
        if(failure==3){f.owner->registered.reset();f.state.reset();}
        Check(!skill->HudSnapshot(30,snapshot), "snapshot refuses inactive or expired owner/state");
    }
    for (int type : {1, 29})
    {
        Fixture f(type); auto base = f.Make(); auto skill = std::dynamic_pointer_cast<CSOLScenarioDamageSkill>(base);
        Check(skill && skill->Type() == type && f.error.message.empty(), "factory supplies the actual selected damage capability");
        CSOLMonsterDamageCall call{&f.player}; bool enabled = true;
        Check(skill->ActiveForMonsterHit(call, enabled, f.error) && !enabled, "new production skill has no active duration");
        Check(skill->AutoActivateOnBossDamage(call, enabled, f.error) && !enabled, "verified v14 disables automatic boss activation");
        enabled = true;
        Check(skill->UsesDamageProgress(call, enabled, f.error) && !enabled, "verified v1C does not request damage progress");
        Check(skill->MonsterDamaged(call, 13.25f, f.error) && !call.cancelled, "ordinary-hit callback completes through its real production implementation");
        Check(skill->BossDamaged(call, 200, f.error) && !call.cancelled, "boss-hit callback completes through its real production implementation");
        Check(f.owner->stopCalls == (type == 29 ? 2 : 0), "only cloak subtype delegates both hit paths to the owning registry");
        if (type == 29) Check(f.owner->stoppedBy == base.get(), "cloak owner receives the actual ability identity");
        for (float now : {10.f, 59.999f, 60.f, 120.f})
        {
            globals.time = now; enabled = true;
            Check(skill->ActiveForMonsterHit(call, enabled, f.error) && !enabled, "damage and time do not invent skill activation");
        }
        CSOLMonsterDeathCall death; enabled = true;
        Check(!skill->Ready(death, enabled, f.error) && !enabled && !f.error.message.empty(), "unimplemented readiness cannot fabricate success");
        f.error = {}; Check(!skill->Activate(death, f.error) && !f.error.message.empty(), "unimplemented activation fails explicitly");
        f.error = {}; Check(!skill->ActiveMonsterHit(call, f.error) && !f.error.message.empty(), "unsupported type12 effect fails explicitly");
        f.error = {}; Check(!skill->ActivateOnBossDamage(call, f.error) && !f.error.message.empty(), "unsupported boss activation fails explicitly");
        f.error = {}; Check(!skill->AdvanceDamageProgress(call, f.error) && !f.error.message.empty(), "unsupported progress sender fails explicitly");
    }
    for (int condition = 0; condition < 8; ++condition)
    {
        Fixture f(29); auto skill = std::dynamic_pointer_cast<CSOLScenarioDamageSkill>(f.Make());
        CSOLMonsterDamageCall call{&f.player}; CBasePlayer other;
        if (condition == 0) f.owner->active = false;
        if (condition == 1) f.owner->registered.reset();
        if (condition == 2) ++f.state->generation;
        if (condition == 3) call.valid = false;
        if (condition == 4) call.player = &other;
        if (condition == 5) f.state->player.value = &other;
        if (condition == 6) f.owner.reset();
        if (condition == 7) { f.owner->registered.reset(); f.state.reset(); }
        Check(!skill->MonsterDamaged(call, 1, f.error) && call.cancelled, "retired owner/state/lease/player cancels before cloak access");
        if (f.owner) Check(f.owner->stopCalls == 0, "invalid owner boundary never reaches registry mutation");
    }
    for (int type : {0, 2, 12, 55})
    {
        Fixture f(type); Check(!f.Make() && f.error.message.empty(), "unverified subtype remains absent for caller diagnostic");
    }
    {
        Fixture f(1); f.definition.type = 9;
        Check(!f.Make() && f.error.message.empty(), "non-active ClassMod category is not an ability");
    }
    for (int condition = 0; condition < 7; ++condition)
    {
        Fixture f(1);
        if (condition == 0) f.owner.reset();
        if (condition == 1) f.state.reset();
        if (condition == 2) f.owner->active = false;
        if (condition == 3) f.owner->registered.reset();
        if (condition == 4) globals.time = std::numeric_limits<float>::quiet_NaN();
        if (condition == 5) f.definition.values[0] = std::numeric_limits<float>::infinity();
        if (condition == 6) f.definition.values[1] = std::numeric_limits<float>::quiet_NaN();
        Check(!f.Make() && !f.error.message.empty(), "invalid admission or non-finite timer is rejected");
    }
    {
        Fixture f(29); auto skill = std::dynamic_pointer_cast<CSOLScenarioDamageSkill>(f.Make());
        CSOLMonsterDamageCall call{&f.player};
        f.owner->onStop = [](auto&, auto& error) { error.message = "owner failure"; return false; };
        Check(!skill->MonsterDamaged(call, 5, f.error) && f.error.message == "owner failure", "cloak owner error is retained");
        f.error = {}; f.owner->onStop = [](auto& call, auto&) { return call.Cancel(); };
        Check(!skill->BossDamaged(call, 5, f.error) && call.cancelled, "cloak callback cancellation propagates");
    }
    {
        Fixture f(1); f.owner->selected = f.definition;
        Check(f.owner->RefreshActiveSkill(f.state, 8196, f.error), "real refresh installs selected skill");
        const auto original = f.state->activeSkill;
        auto& messages = f.owner->m_session->messages;
        Check(original && original->Type() == 1 && messages.calls == 1 && messages.subtype == 1 &&
            messages.cooldown == 50 && messages.duration == 0 && messages.recipient == f.player.edict(),
            "initial cooldown notification reaches the selected player once");
        globals.time = 100;
        Check(f.owner->RefreshActiveSkill(f.state, 8196, f.error) && f.state->activeSkill == original && messages.calls == 1,
            "same class/definition retains object and does not reset cooldown notification");
        ++f.state->profileRevision; ++f.state->classModRevision;
        Check(f.owner->RefreshActiveSkill(f.state, 8196, f.error) && f.state->activeSkill == original && messages.calls == 1,
            "unrelated profile revision with same selected definition preserves skill");
        f.owner->selected->subtype = 29;
        Check(f.owner->RefreshActiveSkill(f.state, 8814, f.error) && f.state->activeSkill != original &&
            f.state->activeSkill->Type() == 29 && messages.calls == 2, "different role replaces skill exactly once");
        f.owner->selected.reset();
        Check(f.owner->RefreshActiveSkill(f.state, 49, f.error) && !f.state->activeSkill && messages.calls == 3 &&
            messages.subtype == 0 && messages.cooldown == 0 && messages.duration == 0,
            "no-skill role clears previous role icon with the original zero-type message");
        Check(f.owner->RefreshActiveSkill(f.state, 49, f.error) && messages.calls == 3,
            "unchanged empty role does not resend initialization every refresh");
        f.owner->selected = f.definition; f.owner->selected->subtype = 55;
        Check(f.owner->RefreshActiveSkill(f.state, 9000, f.error) && !f.state->activeSkill &&
            f.state->activeSkillDefinition->subtype == 55 && messages.calls == 4 && messages.subtype == 0,
            "unsupported selected factory clears presentation but preserves selected definition for failure diagnosis");
    }
    for (int boundary = 0; boundary < 4; ++boundary)
    {
        Fixture f(1); f.owner->selected = f.definition;
        if (boundary == 0) f.owner->queryValid = false;
        if (boundary == 1) f.owner->onSet = [&] { ++f.state->profileRevision; };
        if (boundary == 2) f.owner->onSet = [&] { f.owner->active = false; };
        if (boundary == 3) f.owner->m_session->messages.onSend = [&] { f.owner->registered.reset(); return true; };
        Check(!f.owner->RefreshActiveSkill(f.state, 8196, f.error), "refresh cancellation or unknown selection cannot report success");
        Check(f.owner->m_session->messages.calls == (boundary == 3 ? 1 : 0), "cancelled setup never sends after invalidation");
    }
    {
        Fixture f(1); f.owner->selected.reset();
        f.owner->m_session->messages.onSend = [] { return false; };
        Check(!f.owner->RefreshActiveSkill(f.state, 49, f.error) && !f.error.message.empty(),
            "failed zero-type clear notification is not reported as successful refresh");
        f.owner->m_session->messages.onSend = {}; f.error = {};
        Check(f.owner->RefreshActiveSkill(f.state, 49, f.error) && f.owner->m_session->messages.calls == 2,
            "failed clear notification is retried after the sender becomes available");
    }
    std::cout << "PASS actual CharacterSkill factory/callbacks: " << checks << " checks\n";
    const auto senderChecks = RunSenderTests();
    std::cout << "PASS actual active-skill sender: " << senderChecks << " checks\n";
    const auto resourceChecks = RunResourceTests();
    std::cout << "PASS actual HUD resource precache block: " << resourceChecks << " checks\n";
    const auto hudChecks = RunRuntimeHudTests();
    std::cout << "PASS actual runtime HUD update: " << hudChecks << " checks\n";
}
