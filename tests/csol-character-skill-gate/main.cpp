// SPDX-License-Identifier: GPL-3.0-or-later
// Exact GateSnapshot body + real activation/effect/input implementations.
// Engine action playback and runtime identity are explicit controlled edges.
#include "native/csol/server/scenario_character_skills/control.hpp"
#include "native/csol/server/scenario_character_skills/catalog.hpp"
#include "native/csol/client/character_skill_input.hpp"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>

using namespace csol::scenario::skills;
using csol::scenario::ConfigError;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* why)
{ ++checks; if (!value) { ++failures; std::printf("FAIL: %s\n", why); } }
struct Life { std::uint64_t skillLifeSerial{1}; };
struct Host final : ActivationHost
{
    bool current{true}, alive{true}, blocked{}, target{true}, playing{}, constrained{};
    bool actionAccepted{true}, prepared{true}, failPlaying{}, errorPlaying{};
    float now{100};
    int starts{}, active{}, ends{}, updates{}, playingReads{};
    std::function<void()> onPlaying;
    bool Current() const override { return current; }
    bool ReadTime(float& value, ConfigError&) const override { value=now; return true; }
    bool FrozenWrathBlocks(bool& value, ConfigError&) override { value=blocked; return true; }
    bool PlayerAlive(bool& value, ConfigError&) override { value=alive; return true; }
    bool ScenarioTargetAvailable(bool& value, ConfigError&) override { value=target; return true; }
    bool NotifyNotReady(ConfigError&) override { return true; }
    bool StartCharacterAction5(bool& value, ConfigError&) override
    { value=actionAccepted; playing=value; return true; }
    bool PrepareEffects(ActivationState& state, bool& value, ConfigError&) override
    { value=prepared; state.delay=1; return true; }
    bool AddActionConstraint33(float, ConfigError&) override { constrained=true; return true; }
    bool AdjustCooldown(float requested, float& value, ConfigError&) override
    { value=requested; return true; }
    bool PlayerModelId(int& value, ConfigError&) override { value=133; return true; }
    bool PlayActivationVoice(ConfigError&) override { return true; }
    bool PublishActivation(int, bool, float, ConfigError&) override { return true; }
    bool HasActionConstraint33(bool& value, ConfigError&) override { value=constrained; return true; }
    bool CharacterActionPlaying(bool& value, ConfigError& error) override
    {
        ++playingReads; value=playing; if (onPlaying) onPlaying();
        if (errorPlaying) error.message="action observation failed";
        return !failPlaying && Current();
    }
    bool RemoveActionConstraint33(ConfigError&) override { constrained=false; return true; }
    bool StartEffects(ConfigError&) override { ++starts; return true; }
    bool ActiveEffects(ConfigError&) override { ++active; return true; }
    bool EndEffects(ConfigError&) override { ++ends; return true; }
    bool UpdateEffects(ConfigError&) override { ++updates; return true; }
};
struct Base
{
    virtual bool GateSnapshot(float, csol::skillgate::Snapshot&, ConfigError&)=0;
};
struct CharacterSkill : Base
{
    bool current{true}, bound{true}, m_disabled{};
    int m_type{23};
    SkillTraits m_traits{*FindSkill(23)};
    ActivationState m_timers;
    std::weak_ptr<Life> m_state;
    std::shared_ptr<ActivationHost> m_host;
    bool Current() const { return current && !m_state.expired(); }
    bool BindCurrent() const { return bound; }
#include "gate.inc"
};
struct Fixture
{
    std::shared_ptr<Life> life=std::make_shared<Life>();
    std::shared_ptr<Host> host=std::make_shared<Host>();
    CharacterSkill skill;
    csol::client::CharacterSkillInput input;
    ConfigError error;
    Fixture(int type=23)
    {
        skill.m_type=type;skill.m_traits=*FindSkill(type);skill.m_state=life;skill.m_host=host;
        skill.m_timers.cooldown=40;skill.m_timers.readyAt=100;skill.m_timers.effects.duration=3;
        input.Restore(csol::abilities::Snapshot{unsigned(type),40,40,0},100);
    }
    csol::skillgate::Snapshot Snapshot()
    {
        csol::skillgate::Snapshot gate;
        Check(skill.GateSnapshot(host->now,gate,error),"actual GateSnapshot accepts current owner");
        std::array<std::uint8_t,csol::skillgate::MessageBytes> bytes;
        Check(csol::skillgate::Encode(gate,bytes),"server gate encodes");
        csol::skillgate::Snapshot decoded;
        Check(csol::skillgate::Decode(bytes,decoded),"actual client wire decoder accepts gate");
        input.Restore(decoded,host->now);return gate;
    }
};
void Interrupted(int type, bool ended)
{
    using namespace csol::skillgate;
    Fixture f(type);auto& h=*f.host;auto& s=f.skill;
    Check(f.Snapshot().flags==(Manual|csol::skillgate::Ready),"initial skill is ready");
    Check(f.input.Request(h.now,true),"client permits initial activation");
    Check(Activate(s.m_traits,s.m_timers,h,f.error)==ActivationResult::Started,"first native activation starts action");
    Check(s.m_timers.readyAt==140 && s.m_timers.effects.pendingAt==101,"cast owns original cooldown and delayed start");
    Check(f.Snapshot().flags==(Manual|Preparing),"only current action owns preparation gate");
    Check(!f.input.Request(h.now,true),"client refuses duplicate cast during preparation");
    // The production action owner can end naturally or be cancelled by input,
    // weapon deploy, death, or retirement. Neither means the effect started.
    h.playing=false;h.now=ended?103:100.5f;
    Check(Update(s.m_timers,h,f.error),"interrupted action update succeeds");
    Check(h.starts==0 && !h.constrained,"interruption removes constraint without starting effect");
    Check(s.m_timers.effects.pendingAt==101 && s.m_timers.effects.activeUntil==-1,
        "original cancelled timestamp retained without inventing activation");
    Check(f.Snapshot().flags==Manual,"cancelled timestamp must not advertise Preparing");
    Check(!f.input.Request(h.now,true) && s.m_timers.readyAt==140,"interruption does not refund cooldown");
    h.now=140;
    Check(Update(s.m_timers,h,f.error) && h.starts==0,"expired old timestamp does not release cancelled effect");
    Check(f.Snapshot().flags==(Manual|csol::skillgate::Ready),"cancelled preparation permits recast at original cooldown equality");
    Check(f.input.Request(h.now,true),"actual client can request recast after cancellation");
    Check(Activate(s.m_traits,s.m_timers,h,f.error)==ActivationResult::Started,"second native cast accepted");
    Check(s.m_timers.readyAt==180 && s.m_timers.effects.pendingAt==141,"new cast replaces old timestamp normally");
    h.now=141;Check(Update(s.m_timers,h,f.error) && h.starts==0,"strict original effect threshold preserved");
    h.now=141.125f;Check(Update(s.m_timers,h,f.error) && h.starts==1,"uninterrupted second cast starts once");
    Check(s.m_timers.effects.pendingAt==-1 && s.m_timers.effects.activeUntil==144.125f,"active timer starts at execution");
    Check(f.Snapshot().flags==(Manual|Active),"real started effect advertises Active");
    h.now=145;Check(Update(s.m_timers,h,f.error) && h.starts==1 && h.ends==1,"effect ends without old pending replay");
}
}
int main()
{
    using namespace csol::skillgate;
    for (int type : {1,23,29,54,55}) for (bool ended : {false,true}) Interrupted(type,ended);
    // Reproduce the captured server values, independent of the sequence fixture.
    {
        Fixture f;f.skill.m_timers.readyAt=1622.537842f;f.skill.m_timers.effects={0,1583.537842f,-1};
        f.host->now=2732.534668f;
        Check(f.Snapshot().flags==(Manual|csol::skillgate::Ready),"captured ready character133 no longer stuck Preparing");
        Check(f.input.Request(f.host->now,true),"captured state reaches real client request");
        Check(f.skill.m_timers.effects.pendingAt==1583.537842f,"gate observation does not mutate original timer");
    }
    for (const auto& traits : AllSkills())
    {
        Fixture f(traits.subtype);f.host->now=160;f.skill.m_timers.effects.pendingAt=101;
        const auto gate=f.Snapshot();
        Check(!(gate.flags&Preparing),"all stopped action families stop advertising preparation");
        Check(bool(gate.flags&Manual)==!traits.automaticTrigger,"manual/automatic ownership preserved");
        Check(f.input.Request(160,true)==!traits.automaticTrigger,
            "manual permission follows original readiness across catalog");
    }
    {
        Fixture f;f.skill.m_traits.damageProgress=true;f.skill.m_timers.requiredProgress=3;
        f.skill.m_timers.effects.pendingAt=101;f.host->now=160;
        Check(f.Snapshot().flags==Manual && !f.input.Request(160,true),"unfinished progress still refuses recast");
        f.skill.m_timers.progress=3;
        Check(f.Snapshot().flags==(Manual|csol::skillgate::Ready) && f.input.Request(160,true),"completed progress follows original readiness");
    }
    for (int block=0;block<3;++block)
    {
        Fixture f(block==2?21:23);f.skill.m_timers.effects.pendingAt=101;f.host->now=150;
        if(block==0)f.host->alive=false;if(block==1)f.host->blocked=true;if(block==2)f.host->target=false;
        const auto gate=f.Snapshot();Check((gate.flags&Blocked)&&!(gate.flags&(Preparing|csol::skillgate::Ready)),"actual gameplay refusal preserved");
        Check(!f.input.Request(150,true),"client cannot override server refusal");
    }
    for (int failure=0;failure<6;++failure)
    {
        Fixture f;f.skill.m_timers.effects.pendingAt=101;
        if(failure==0)f.host->failPlaying=true;
        if(failure==1)f.host->errorPlaying=true;
        if(failure==2)f.host->onPlaying=[&]{f.skill.current=false;};
        if(failure==3)f.host->onPlaying=[&]{++f.life->skillLifeSerial;};
        if(failure==4)f.host->onPlaying=[&]{f.life.reset();};
        if(failure==5)f.host->onPlaying=[&]{f.host->current=false;};
        Snapshot gate{9,Manual|csol::skillgate::Ready,0,0};
        Check(!f.skill.GateSnapshot(150,gate,f.error),"failed or retired playback read cannot publish stale readiness");
        Check(gate.subtype==9 && gate.flags==(Manual|csol::skillgate::Ready),"failed observation leaves caller output unchanged");
    }
    {
        Fixture f;f.skill.m_disabled=true;
        Check(f.Snapshot().subtype==0 && !f.input.OwnsKey(),"disabled capability clears activation ownership");
        Check(f.host->playingReads==0,"disabled skill does not observe retired action");
    }
    {
        Fixture f;f.skill.m_timers.effects={10,-1,200};f.host->now=160;
        Check(f.Snapshot().flags==(Manual|csol::skillgate::Ready|Active),"long effect may coexist with ready original cooldown");
        Check(f.host->playingReads==0,"finished preparation does not add an action callback");
    }
    std::printf("skill gate checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
