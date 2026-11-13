// SPDX-License-Identifier: GPL-3.0-or-later
#define main ExistingKickFixtureMain
#include "../csol-human-kick-server/sdk.cpp"
#undef main
#include "../../native/csol/scenario/custom/challenge_cycle.hpp"
#include "../../native/csol/scenario/custom/settings.hpp"
#include "../../native/csol/server/custom/action_stamina.h"
#include "goldcraft/csol_scenario_admin_api.hpp"
#include <filesystem>
#include <fstream>

namespace phase_sdk
{
using namespace kick_test;
namespace custom = csol::scenario::custom;
unsigned playbackEvents{};
void Playback(int flags,const edict_t* invoker,unsigned short event,float delay,float*,float*,float,float,int,int,int,int)
{
    // Exact eiface.h media transport boundary. Record the actual event without
    // simulating a client effect or changing authoritative round state.
    ++playbackEvents;
    Engine::self->Record("phase-playback:"+std::to_string(flags)+":"+std::to_string(event)+":"+
        std::to_string(delay)+":"+std::to_string(invoker?Engine::Index(invoker):0));
}
void Settings(bool enabled)
{
    custom::Draft draft; custom::Error error; custom::SnapshotPtr snapshot;
    Require(draft.Set(custom::Feature::TimeCycle,enabled,error) &&
        draft.Set(custom::Setting::MiddaySeconds,2,error) &&
        draft.Set(custom::Setting::DuskSeconds,3,error) && draft.Finish(snapshot,error),
        "actual grouped settings prepare two-second noon and three-second dusk");
    const auto path=std::filesystem::current_path()/"cstrike/addons/goldcraft/config/gameplay.json";
    Require(!std::filesystem::exists(path),"fresh SDK game directory");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);out<<custom::Serialize(*snapshot);
    Require(bool(out),"persist settings before actual GameDLL initialization");
}
void Restart(Case& c)
{
    auto* rules=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);
    Require(rules!=nullptr,"actual installed multiplayer rules");
    const auto before=c.f.session->Map().RoundGeneration();
    // Preserve this focused test actor while the actual rules commit their
    // stage; native player-reset/respawn is independently covered elsewhere.
    const auto menu=c.player.m_iMenu;c.player.m_iMenu=Menu_ChooseAppearance;
    Require(CSOL_ScenarioRestartRound(*rules),"real ReAPI scenario restart wrapper consumes the map");
    c.player.m_iMenu=menu;
    Require(!CSOL_RulesRejected() && c.f.session->Map().RoundGeneration()==before+1,
        "actual GameRules restart commits one new stage generation");
}
custom::ChallengePhaseView Read(Case& c)
{
    custom::ChallengePhaseView view;
    Require(CSOL_ReadChallengePhase(c.f.runtime,view),"actual current rules expose authoritative phase");
    return view;
}
void Tick(Case& c,float now)
{
    c.f.engine.globals.time=now;
    Require(CSOL_ScenarioThink(*static_cast<CHalfLifeMultiplay*>(g_pGameRules)),"actual rules Think consumes scenario frame");
    Require(!CSOL_RulesRejected(),"phase update does not reject the map");
    custom::ChallengePhaseView observed;
    if(CSOL_ReadChallengePhase(c.f.runtime,observed))
        std::cout<<"phase now="<<now<<" value="<<unsigned(observed.time.phase)
            <<" remaining="<<observed.time.remaining<<" preparing="<<observed.preparing
            <<" health="<<c.player.pev->health<<" terminating="
            <<static_cast<CHalfLifeMultiplay*>(g_pGameRules)->m_bRoundTerminating<<'\n';
}
void NativeActor(Case& c)
{
    g_engfuncs.pfnPlaybackEvent=Playback;
    // The kick component constructs private player state without running
    // ClientPutInServer. Real GameRules counts through the entity hash.
    AddEntityHashValue(c.player.pev,STRING(c.player.pev->classname),CLASSNAME);
    int aliveT{},aliveCt{},deadT{},deadCt{};
    static_cast<CHalfLifeMultiplay*>(g_pGameRules)->InitializePlayerCounts(aliveT,aliveCt,deadT,deadCt);
    Require(aliveCt==1 && deadCt==0,"actual native entity hash/counting sees the living CT actor");
    // Case's combat probe stands at the trigger/spawn origin. This clock
    // fixture starts outside those authored volumes so it does not win or
    // die before its two-second transition can be observed.
    c.player.pev->origin=Vector(1000,1000,128);
}
void Flow(bool enabled)
{
    Settings(enabled);Case c;NativeActor(c);
    custom::ChallengePhaseView absent{{custom::Phase::Night,77},false,88};
    Expect(!CSOL_ReadChallengePhase(c.f.runtime,absent) && absent.challenge==88,
        "uncommitted stage never invents a running clock or alters caller output");
    Restart(c);
    if(!enabled)
    {
        Expect(!CSOL_ReadChallengePhase(c.f.runtime,absent),"disabled policy leaves native stage without custom clock");
        Require(c.Update(),"ordinary disabled-time kick owner still updates");
        const auto stamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
        Expect(stamina && !stamina->infinite,"disabled time keeps finite emergency stamina");
        return;
    }
    auto view=Read(c);
    Expect(view.preparing && view.time.phase==custom::Phase::Noon && view.time.remaining==2,
        "committed stage preparation keeps the configured noon duration");
    Require(c.Update(),"real kick owner consumes current authoritative phase");
    const auto noonStamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
    Expect(noonStamina && noonStamina->infinite,"preparation noon reaches real kick stamina");
    Tick(c,120);
    view=Read(c);
    Expect(!view.preparing && view.time.phase==custom::Phase::Noon && view.time.remaining==2,
        "actual GameRules thaw starts challenge time");
    Tick(c,122);view=Read(c);
    Expect(view.time.phase==custom::Phase::Dusk && view.time.remaining==3,
        "actual Think advances exact noon boundary to dusk");
    Require(c.Update(),"dusk reaches real action-stamina owner");
    const auto stamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
    Expect(stamina && !stamina->infinite && stamina->maximum==150,"actual kick stamina switches to enhanced finite dusk");
    Tick(c,125);view=Read(c);
    Expect(view.time.phase==custom::Phase::Night && view.time.remaining==0,"night has no fabricated countdown");
    Require(c.Update(),"night reaches action-stamina owner");
    const auto nightStamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
    Expect(nightStamina && nightStamina->maximum==100,"night restores normal finite capacity");
    const auto challenge=view.challenge;
    Restart(c);view=Read(c);
    Expect(!view.preparing && view.time.phase==custom::Phase::Night && view.challenge==challenge,
        "ordinary real stage restart does not reset eternal night");
    const auto foreign=std::make_shared<CCSOLScenarioRuntime>();
    Expect(!CSOL_ReadChallengePhase(foreign,absent),"foreign runtime cannot read another rules clock");
    c.f.runtime->RequestClose();
    Expect(!CSOL_ReadChallengePhase(c.f.runtime,absent),"retired owner immediately loses phase authority");
}
void FailureAndReplay(bool replay)
{
    Settings(true);Case c;NativeActor(c);Restart(c);Tick(c,120);
    const auto original=Read(c);
    c.player.pev->health=0;c.player.pev->deadflag=DEAD_DEAD;
    Tick(c,121);
    auto view=Read(c);
    Expect(view.time.phase==custom::Phase::Ended && !view.preparing,
        "real all-CT-dead rule ends custom time without disabling the original failure condition");
    Expect(!CSOL_ActionStaminaCanKick(c.f.runtime,c.player),"dead player cannot retain noon kick authority");
    if(!replay)
    {
        Tick(c,125);view=Read(c);
        Expect(view.time.phase==custom::Phase::Ended && view.challenge==original.challenge,
            "later failed-round frames cannot create another challenge");
        return;
    }
    auto* service=static_cast<csol::IScenarioReplayService*>(CreateInterface(csol::ScenarioReplayServiceVersion,nullptr));
    Require(service!=nullptr,"actual administrator replay interface is exported");
    Expect(!service->RequestReplay(99),"foreign map replay cannot change the current clock");
    Require(service->RequestReplay(98),"actual replay service accepts this loaded map");
    Expect(!service->RequestReplay(98),"duplicate queued replay cannot create two challenges");
    view=Read(c);
    Expect(view.challenge==original.challenge && view.time.phase==custom::Phase::Ended,
        "accepted but uncommitted replay does not prematurely reset phase state");
    const auto round=c.f.session->Map().RoundGeneration();
    c.player.m_iMenu=Menu_ChooseAppearance;
    Tick(c,122);
    view=Read(c);
    Expect(c.f.session->Map().RoundGeneration()==round+1 && view.challenge==original.challenge+1 &&
        view.preparing && view.time.phase==custom::Phase::Noon && view.time.remaining==2,
        "safe real Think commits replay and resets exactly one pending noon challenge");
}
}
int main(int argc,char** argv)
{
    try
    {
        sdk_test::Require(argc==2,"one phase SDK group");
        const std::string_view group=argv[1];
        if(group=="enabled"||group=="disabled")phase_sdk::Flow(group=="enabled");
        else if(group=="failed"||group=="replay")phase_sdk::FailureAndReplay(group=="replay");
        else sdk_test::Require(false,"known phase SDK group");
    }
    catch(const std::exception& error){++sdk_test::failures;std::cerr<<"ERROR "<<error.what()<<'\n';}
    std::cout<<"behaviorChecks="<<kick_test::behaviorChecks<<" checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
    return sdk_test::failures?1:0;
}
