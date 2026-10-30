// SPDX-License-Identifier: GPL-3.0-or-later
// Actual UpdateHud body; the registry, upgrade publisher and transport are
// controlled boundary objects. This is not the full ReGameDLL player loop.
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../../native/csol/scenario_abilities.hpp"
#include "../../native/csol/scenario/config.hpp"

namespace hudruntime
{
struct Edict {};
struct CBasePlayer { Edict entity; Edict* edict(){return &entity;} };
struct EHANDLE
{
    CBasePlayer* value{};
    EHANDLE(CBasePlayer* player):value(player){}
    template<class T>T* Get()const{return static_cast<T*>(value);}
};
std::vector<std::string> operations;
struct Upgrade
{
    int configured{8196};
    csol::scenario::ConfigError error;
    std::function<bool()> onPublish,onUpdate;
    int ConfiguredClass()const{return configured;}
    bool Publish(){operations.push_back("levels");return onPublish?onPublish():true;}
    bool Update(float,bool force){operations.push_back(force?"xp-force":"xp");return onUpdate?onUpdate():true;}
    const auto& Error()const{return error;}
};
struct Skill
{
    std::function<bool()> onSnapshot;
    bool HudSnapshot(float,csol::abilities::Snapshot& snapshot)
    {operations.push_back("capture");snapshot={29,50,20,0};return onSnapshot?onSnapshot():true;}
};
struct CSOLScenarioPlayerRuntimeState
{
    std::uint64_t profileRevision{1},activeSkillRevision{1};
    std::shared_ptr<Skill> activeSkill=std::make_shared<Skill>();
    bool updatingHud{},hudReplayPending{};
};
struct Registry
{
    std::shared_ptr<Upgrade> upgrade=std::make_shared<Upgrade>();
    auto Upgrades(EHANDLE){return upgrade;}
};
struct Messages
{
    csol::abilities::Snapshot last;
    std::function<bool()> onSend;
    bool SendActiveSkillSnapshot(Edict*,const csol::abilities::Snapshot& snapshot)
    {operations.push_back("send");last=snapshot;return onSend?onSend():true;}
};
struct Session
{
    std::shared_ptr<Registry> registry=std::make_shared<Registry>();
    ::hudruntime::Messages messages;
    auto Players(){return registry;}
    auto& Messages(){return messages;}
};
struct CCSOLScenarioPlayerRuntime : std::enable_shared_from_this<CCSOLScenarioPlayerRuntime>
{
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> state=std::make_shared<CSOLScenarioPlayerRuntimeState>();
    std::shared_ptr<Session> m_session=std::make_shared<Session>();
    std::string error;
    auto Resolve(EHANDLE){return state;}
    bool Fail(const char* text){error=text;return false;}
    bool UpdateHud(CBasePlayer&,float,bool);
};
#include "actual_update_hud.inc"
int checks{};
void Check(bool result,const char* text)
{
    ++checks;if(!result){std::cerr<<"FAIL runtime HUD "<<checks<<": "<<text<<'\n';std::exit(1);}
}
}

int RunRuntimeHudTests()
{
    using namespace hudruntime;
    {
        CBasePlayer player;auto owner=std::make_shared<CCSOLScenarioPlayerRuntime>();operations.clear();
        const auto skill=owner->state->activeSkill;
        Check(owner->UpdateHud(player,30,true),"forced HUD snapshot completes");
        Check(operations==std::vector<std::string>{"levels","xp-force","capture","send"},
            "static levels and XP precede skill snapshot");
        Check(owner->state->activeSkill==skill && owner->state->activeSkillRevision==1 &&
            owner->m_session->messages.last.elapsed==20,"snapshot preserves original ability identity and elapsed clock");
        operations.clear();Check(owner->UpdateHud(player,31,false),"normal HUD update succeeds");
        Check(operations==std::vector<std::string>{"xp"},"normal frames do not resend skill or static levels");
        owner->state->activeSkill.reset();operations.clear();
        Check(owner->UpdateHud(player,31,true) && owner->m_session->messages.last.type==0 &&
            owner->m_session->messages.last.cooldown==0,"missing skill clears previous icon on replay");
    }
    for(int boundary=0;boundary<8;++boundary)
    {
        CBasePlayer player;auto owner=std::make_shared<CCSOLScenarioPlayerRuntime>();
        auto state=owner->state;auto upgrade=owner->m_session->registry->upgrade;operations.clear();
        if(boundary==0)upgrade->onPublish=[&]{owner->state.reset();return true;};
        if(boundary==1)upgrade->onPublish=[&]{++state->profileRevision;return true;};
        if(boundary==2)upgrade->onUpdate=[&]{owner->state.reset();return true;};
        if(boundary==3)upgrade->onUpdate=[&]{++state->activeSkillRevision;return true;};
        if(boundary==4)upgrade->onUpdate=[&]{owner->m_session->registry->upgrade=std::make_shared<Upgrade>();return true;};
        if(boundary==5)state->activeSkill->onSnapshot=[&]{owner->state.reset();return true;};
        if(boundary==6)state->activeSkill->onSnapshot=[&]{state->activeSkill=std::make_shared<Skill>();return true;};
        if(boundary==7)owner->m_session->messages.onSend=[&]{owner->state.reset();return true;};
        Check(!owner->UpdateHud(player,30,true),"retired state/profile/skill/upgrade cannot report successful replay");
        Check(operations.back()==(boundary<2?"levels":boundary<5?"xp-force":boundary<7?"capture":"send"),
            "retirement stops before the next outgoing stage");
    }
    for(int boundary=0;boundary<4;++boundary)
    {
        CBasePlayer player;auto owner=std::make_shared<CCSOLScenarioPlayerRuntime>();operations.clear();
        if(boundary==0)owner->m_session->registry->upgrade->onPublish=[]{return false;};
        if(boundary==1)owner->m_session->registry->upgrade->onUpdate=[]{return false;};
        if(boundary==2)owner->state->activeSkill->onSnapshot=[]{return false;};
        if(boundary==3)owner->m_session->messages.onSend=[]{return false;};
        Check(!owner->UpdateHud(player,30,true) && !owner->error.empty(),"publisher failure is explicit");
        Check(!owner->state->updatingHud,"failed publisher releases HUD recursion guard");
    }
    for(int boundary=0;boundary<4;++boundary)
    for(bool nestedForce : {false,true})
    {
        CBasePlayer player;auto owner=std::make_shared<CCSOLScenarioPlayerRuntime>();operations.clear();
        bool entered{},nestedResult{},guardSeen{};
        const auto callback=[&]
        {
            if(!entered)
            {
                entered=true;guardSeen=owner->state->updatingHud;
                nestedResult=owner->UpdateHud(player,30,nestedForce);
            }
            return true;
        };
        auto upgrade=owner->m_session->registry->upgrade;
        auto skill=owner->state->activeSkill;
        if(boundary==0)upgrade->onPublish=callback;
        if(boundary==1)upgrade->onUpdate=callback;
        if(boundary==2)skill->onSnapshot=callback;
        if(boundary==3)owner->m_session->messages.onSend=callback;
        Check(owner->UpdateHud(player,30,true),"outer snapshot survives real recursive update callback");
        Check(entered && nestedResult && guardSeen && owner->error.empty(),
            "nested update observes held guard and returns without false map rejection");
        Check(operations==std::vector<std::string>{"levels","xp-force","capture","send"},
            "recursive callback cannot enter publisher or transport twice");
        Check(!owner->state->updatingHud && owner->state->hudReplayPending==nestedForce,
            "only nested forced replay is retained after outer guard release");
        upgrade->onPublish={};upgrade->onUpdate={};skill->onSnapshot={};owner->m_session->messages.onSend={};
        operations.clear();
        Check(owner->UpdateHud(player,31,false),"next ordinary frame consumes deferred replay request");
        Check(operations==(nestedForce?std::vector<std::string>{"levels","xp-force","capture","send"}:
            std::vector<std::string>{"xp"}),"pending replay gets one complete next-frame snapshot; normal recursion adds none");
        Check(!owner->state->hudReplayPending && !owner->state->updatingHud && owner->state->activeSkill==skill,
            "next-frame completion clears pending without replacing ability");
    }
    return checks;
}
