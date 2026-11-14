// SPDX-License-Identifier: GPL-3.0-or-later
#define main ExistingKickFixtureMain
#include "../csol-human-kick-server/sdk.cpp"
#undef main
#include "../../native/csol/scenario/custom/challenge_cycle.hpp"
#include "../../native/csol/scenario/custom/phase_snapshot.hpp"
#include "../../native/csol/scenario/custom/settings.hpp"
#include "../../native/csol/server/custom/action_stamina.h"
#include "../../native/csol/server/characters.h"
#include "../../native/csol/scenario/custom/stamina_audio.hpp"
#include "../../native/csol/scenario/custom/stamina_snapshot.hpp"
#include "../../native/csol/scenario/custom/phase_art.hpp"
#include "../../native/csol/scenario/core/scenario_music.hpp"
#include "goldcraft/csol_scenario_admin_api.hpp"
#include <filesystem>
#include <fstream>

namespace phase_sdk
{
using namespace kick_test;
namespace custom = csol::scenario::custom;
unsigned playbackEvents{};
void MusicResources(std::map<std::string,std::string>& files)
{
    kick_test::Resources(files);
    for(const auto file:csol::music::NightFiles)
        files["bgm/"+std::string(file)]="explicit challenge-track existence fixture; audio decoding is not claimed";
    for(const auto* path:{custom::NightAnimationAtlas})
        files[path]="explicit authored-art existence fixture; decoding is independently verified";
}
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
bool chooseRex{};
void NativeActor(Case& c,bool rex=false)
{
    chooseRex=rex;
    g_engfuncs.pfnRandomLong=[](int32 low,int32 high)->int32
    {return low==0&&high==1&&chooseRex?1:low;};
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
void Flow(bool enabled,bool join=false,bool rex=false)
{
    Settings(enabled);Case c(MusicResources);NativeActor(c,rex);
    custom::ChallengePhaseView absent{{custom::Phase::Night,77},false,88};
    Expect(!CSOL_ReadChallengePhase(c.f.runtime,absent) && absent.challenge==88,
        "uncommitted stage never invents a running clock or alters caller output");
    Restart(c);
    if(!enabled)
    {
        Expect(!c.f.engine.messages.contains(custom::PhaseMessage),"disabled policy does not register the extra message");
        Expect(!CSOL_ReadChallengePhase(c.f.runtime,absent),"disabled policy leaves native stage without custom clock");
        Require(c.Update(),"ordinary disabled-time kick owner still updates");
        const auto stamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
        Expect(stamina && !stamina->infinite,"disabled time keeps finite emergency stamina");
        return;
    }
    auto view=Read(c);
    const auto message=c.f.engine.messages.find(custom::PhaseMessage);
    Require(message!=c.f.engine.messages.end(),"enabled policy registers the fixed phase message during map precache");
    const auto countMessages=[&](){return std::count_if(c.packets.begin(),c.packets.end(),[&](const auto& p){return p.id==message->second;});};
    const auto musicMessage=c.f.engine.messages.find(csol::music::MessageName);
    Require(musicMessage!=c.f.engine.messages.end(),"real music service registers its transport");
    const auto countNight=[&]()
    {
        return std::count_if(c.packets.begin(),c.packets.end(),[&](const auto& packet)
        {
            const auto music=packet.id==musicMessage->second?csol::music::Decode(packet.bytes):std::nullopt;
            return music&&(music->path=="bgm/Play_BGM_Rex.mp3"||music->path=="bgm/Zsh_nightbgm.mp3");
        });
    };
    const auto latestMessage=[&]()
    {
        for(auto p=c.packets.rbegin();p!=c.packets.rend();++p)if(p->id==message->second)
        {
            const auto snapshot=custom::DecodePhase(p->bytes);
            Require(snapshot&&p->destination==MSG_ALL&&p->receiver==0,"actual broadcast uses exact valid19-byte phase payload");
            return *snapshot;
        }
        throw std::runtime_error("no authoritative phase message");
    };
    Expect(countMessages()==1&&latestMessage().preparing,"committed preparation broadcasts exactly once");
    Expect(view.preparing && view.time.phase==custom::Phase::Noon && view.time.remaining==2,
        "committed stage preparation keeps the configured noon duration");
    Expect(countNight()==0,"prepared noon never starts the night track");
    Require(c.Update(),"real kick owner consumes current authoritative phase");
    const auto noonStamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
    Expect(noonStamina && noonStamina->infinite,"preparation noon reaches real kick stamina");
    Tick(c,120);
    view=Read(c);
    Expect(!view.preparing && view.time.phase==custom::Phase::Noon && view.time.remaining==2,
        "actual GameRules thaw starts challenge time");
    Expect(countMessages()==2&&!latestMessage().preparing,"actual thaw publishes one countdown origin");
    Tick(c,121);
    Expect(countMessages()==2,"ordinary unchanged Think sends no reliable countdown traffic");
    Tick(c,122);view=Read(c);
    Expect(view.time.phase==custom::Phase::Dusk && view.time.remaining==3,
        "actual Think advances exact noon boundary to dusk");
    Expect(countMessages()==3&&latestMessage().phase==custom::Phase::Dusk,"dusk publishes once through actual message writer");
    const auto duskPacket=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& p){return p.id==musicMessage->second;});
    const auto duskMusic=csol::music::Decode(duskPacket->bytes);
    Expect(duskMusic&&duskMusic->path=="bgm/BGM_zombie_attack.mp3"&&duskMusic->action==csol::music::Action::Play,
        "actual dusk transition plays the human-selected attack BGM once");
    Require(c.Update(),"dusk reaches real action-stamina owner");
    const auto stamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
    Expect(stamina && !stamina->infinite && stamina->maximum==150,"actual kick stamina switches to enhanced finite dusk");
    Tick(c,125);view=Read(c);
    Expect(view.time.phase==custom::Phase::Night && view.time.remaining==0,"night has no fabricated countdown");
    Expect(countMessages()==4&&latestMessage().phase==custom::Phase::Night&&latestMessage().remaining==0,
        "night publishes no fabricated expiry");
    Require(countNight()==1,"actual night transition publishes one original night track");
    const auto nightPacket=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& p){return p.id==musicMessage->second;});
    const auto nightMusic=csol::music::Decode(nightPacket->bytes);
    Expect(nightMusic&&nightPacket->destination==MSG_ALL&&
        (rex?(nightMusic->path=="bgm/Play_BGM_Rex.mp3"&&nightMusic->action==csol::music::Action::PlayThenLoop&&
            nightMusic->nextPath=="bgm/BGM_Rex_loop.mp3"):
            (nightMusic->path=="bgm/Zsh_nightbgm.mp3"&&nightMusic->action==csol::music::Action::Loop)),
        "actual engine random choice selects the requested loop or intro-tail program");
    Require(c.Update(),"night reaches action-stamina owner");
    const auto nightStamina=CSOL_ActionStaminaView(c.f.runtime,c.player);
    Expect(nightStamina && nightStamina->maximum==100,"night restores normal finite capacity");
    const auto challenge=view.challenge;
    Restart(c);view=Read(c);
    Expect(!view.preparing && view.time.phase==custom::Phase::Night && view.challenge==challenge,
        "ordinary real stage restart does not reset eternal night");
    Expect(countNight()==1,"ordinary stage restart does not replay the night cue");
    if(join)
    {
        const auto before=countMessages();
        // Native InitHUD sends the server MOTD before the scenario snapshot.
        // Supply that engine-owned file; keep the fixture's strict free check.
        c.f.engine.files["phase-motd.txt"]="Phase HUD join fixture\n";
        CVAR_SET_STRING("motdfile","phase-motd.txt");
        g_pGameRules->InitHUD(&c.player);
        Require(!CSOL_RulesRejected(),"actual joining-player InitHUD retains the current map");
        Expect(countMessages()==before+1,"late InitHUD replays exactly one phase message");
        const auto p=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& value){return value.id==message->second;});
        const auto snapshot=p!=c.packets.rend()?custom::DecodePhase(p->bytes):std::nullopt;
        Expect(snapshot&&snapshot->phase==custom::Phase::Night&&snapshot->challenge==challenge&&
            p->destination==MSG_ONE&&p->receiver==c.player.entindex(),
            "joining client receives only its own current authoritative night snapshot");
        const auto musicPacket=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& packet){return packet.id==musicMessage->second;});
        const auto replay=csol::music::Decode(musicPacket->bytes);
        Expect(countNight()==2&&replay&&replay->revision==nightMusic->revision&&
            replay->action==nightMusic->action&&replay->path==nightMusic->path&&replay->nextPath==nightMusic->nextPath&&
            musicPacket->destination==MSG_ONE&&musicPacket->receiver==c.player.entindex(),
            "late InitHUD replays the same night cue only to its joining client");
    }
    const auto foreign=std::make_shared<CCSOLScenarioRuntime>();
    Expect(!CSOL_ReadChallengePhase(foreign,absent),"foreign runtime cannot read another rules clock");
    c.f.runtime->RequestClose();
    Expect(!CSOL_ReadChallengePhase(c.f.runtime,absent),"retired owner immediately loses phase authority");
}
void FailureAndReplay(bool replay,bool night=false)
{
    Settings(true);Case c(MusicResources);NativeActor(c);Restart(c);Tick(c,120);
    if(night)Tick(c,125);
    const auto original=Read(c);
    const auto musicMessage=c.f.engine.messages.at(csol::music::MessageName);
    const auto musicCount=[&](){return std::count_if(c.packets.begin(),c.packets.end(),[&](const auto& p){return p.id==musicMessage;});};
    const auto beforeFailure=musicCount();
    c.player.pev->health=0;c.player.pev->deadflag=DEAD_DEAD;
    Tick(c,night?126:121);
    auto view=Read(c);
    Expect(view.time.phase==custom::Phase::Ended && !view.preparing,
        "real all-CT-dead rule ends custom time without disabling the original failure condition");
    Expect(!CSOL_ActionStaminaCanKick(c.f.runtime,c.player),"dead player cannot retain noon kick authority");
    if(night)
    {
        const auto last=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& p){return p.id==musicMessage;});
        const auto restored=csol::music::Decode(last->bytes);
        Expect(musicCount()==beforeFailure+1&&restored&&restored->path!="bgm/Play_BGM_Rex.mp3",
            "real all-dead failure releases night to the retained map intent exactly once");
    }
    if(!replay)
    {
        Tick(c,night?130:125);view=Read(c);
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
    Tick(c,night?127:122);
    view=Read(c);
    Expect(c.f.session->Map().RoundGeneration()==round+1 && view.challenge==original.challenge+1 &&
        view.preparing && view.time.phase==custom::Phase::Noon && view.time.remaining==2,
        "safe real Think commits replay and resets exactly one pending noon challenge");
}
void Breath(bool male,bool cancel=false)
{
    Settings(false);Case c(MusicResources);
    auto& files=c.f.engine.files;
    files["models/csol/characters.json"]=std::string(R"({"version":1,"characters":[{"id":1,"team":2,"skin":0,"male":)")+
        (male?"true":"false")+R"(,"allowPlayerStyle":false,"handStyle":0,"scenarioClass":0,"name":"fixture","model":"fixture","hand":"","portrait":"gfx/csol/p/fixture.tga"}]})";
    files["shader/csol/characters.txt"]="models/player/fixture/fixture.mdl\ngfx/csol/p/fixture.tga\n";
    files["models/player/fixture/fixture.mdl"]="explicit existing studio fixture";
    files["gfx/csol/p/fixture.tga"]="explicit existence fixture";
    CSOL_PrecacheCharacters();
    c.player.pev->flags|=FL_FAKECLIENT;
    const auto selected=CSOL_EnsureScenarioBotCharacter(&c.player,c.f.session->Map().EntityAccess());
    c.player.pev->flags&=~FL_FAKECLIENT;
    Require(selected,"actual bot-character selection binds a parsed catalog entry and userid");
    bool selectedMale{};
    Require(CSOL_CharacterMale(&c.player,selectedMale,c.f.session->Map().EntityAccess())&&selectedMale==male,
        "real character service returns the selected trusted gender");
    csol::scenario::ConfigError error;
    Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error,true),"real force/InitHUD republishes existing stamina authority");
    const auto hudId=c.f.engine.messages.at(custom::StaminaMessage);
    const auto hudCount=[&](){return std::count_if(c.packets.begin(),c.packets.end(),[&](const auto& p){return p.id==hudId;});};
    const auto latestHud=[&]()
    {
        const auto packet=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& p){return p.id==hudId;});
        Require(packet!=c.packets.rend(),"actual stamina HUD transport exists");
        const auto hud=custom::DecodeStamina(packet->bytes);
        Require(hud&&packet->destination==MSG_ONE&&packet->receiver==c.player.entindex(),"stamina goes only to actual player owner");
        return *hud;
    };
    Expect(hudCount()==1&&latestHud().visible&&latestHud().amount==100&&latestHud().maximum==100,"initial real stamina is published exactly once");
    for(std::uint64_t token=1;token<=10;++token)
        Require(CSOL_TryDebitKickStamina(c.f.runtime,c.player,token)==CSOLKickStaminaDebit::Applied,
            "accepted real kick debit consumes the ordinary finite budget");
    const auto id=c.f.engine.messages.at(custom::BreathMessage);
    const auto count=[&](){return std::count_if(c.packets.begin(),c.packets.end(),[&](const auto& p){return p.id==id;});};
    Expect(count()==0,"consuming the last amount does not invent a recovery event before its accepted tick");
    Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"same-frame amount0 remains valid");
    Expect(hudCount()==2&&latestHud().amount==0,"accepted kick debit reaches HUD without inventing recovery");
    Expect(count()==0,"same time before the recovery edge sends no sound");
    if(cancel)c.callback=[&](std::string_view edge){if(edge=="message"&&c.packets.back().id==id)c.f.runtime->RequestClose();};
    const bool stepped=CSOL_TickActionStamina(c.f.runtime,c.player,100.01f,error);
    Require(cancel?!stepped:stepped,"real audio-send callback preserves or retires the exact stamina owner");
    Require(count()==1,"real exhausted recovery edge sends exactly one voice message");
    const auto packet=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& p){return p.id==id;});
    const auto sound=custom::DecodeRecoverySound(packet->bytes);
    Expect(sound&&sound->male==male&&sound->event==1&&packet->destination==MSG_ONE&&packet->receiver==c.player.entindex(),
        "actual transport publishes one matched gender/life/event only to its owner");
    if(cancel)
    {
        Expect(!CSOL_TickActionStamina(c.f.runtime,c.player,101,error)&&count()==1,
            "retired map cannot publish later breathing");
        return;
    }
    Expect(hudCount()==3&&latestHud().exhausted&&latestHud().amount==.1f,"exhaustion HUD carries exact authoritative wait state");
    for(int i=0;i<100;++i)Require(CSOL_TickActionStamina(c.f.runtime,c.player,100.02f+i*.001f,error),"accepted recovery wait tick");
    Expect(hudCount()==3,"100 identical recovery-wait frames do not flood the reliable channel");
    Expect(count()==1,"100 frames in the same exhausted episode never repeat the sound");
    Require(CSOL_TickActionStamina(c.f.runtime,c.player,105.02f,error),"actual original recovery delay finishes");
    Expect(!CSOL_ActionStaminaView(c.f.runtime,c.player)->exhausted&&count()==1,"recovery completion does not replay the start cue");
    Expect(hudCount()==4&&!latestHud().exhausted&&latestHud().amount>1,"actual recovery amount reaches HUD");
    CSOL_RetireActionStamina(c.player);
    Expect(hudCount()==5&&!latestHud().visible,"retirement clears only this owner's stamina HUD");
    Expect(!CSOL_TickActionStamina(c.f.runtime,c.player,106,error)&&count()==1,"native life retirement cannot resurrect old sound");
}
void HudCancel()
{
    Settings(false);Case c(MusicResources);csol::scenario::ConfigError error;
    const auto message=c.f.engine.messages.at(custom::StaminaMessage);
    c.callback=[&](std::string_view edge){if(edge=="message"&&c.packets.back().id==message)c.f.runtime->RequestClose();};
    Expect(!CSOL_TickActionStamina(c.f.runtime,c.player,100,error,true),"HUD transport reentry retires actual runtime");
    const auto count=c.packets.size();
    Expect(!CSOL_TickActionStamina(c.f.runtime,c.player,101,error)&&c.packets.size()==count,"closed owner cannot publish another HUD update");
}
}
int main(int argc,char** argv)
{
    try
    {
        sdk_test::Require(argc==2,"one phase SDK group");
        const std::string_view group=argv[1];
        if(group=="enabled"||group=="disabled")phase_sdk::Flow(group=="enabled");
        else if(group=="join")phase_sdk::Flow(true,true);
        else if(group=="rex"||group=="rex-join")phase_sdk::Flow(true,group=="rex-join",true);
        else if(group=="failed"||group=="replay")phase_sdk::FailureAndReplay(group=="replay");
        else if(group=="night-failed"||group=="night-replay")phase_sdk::FailureAndReplay(group=="night-replay",true);
        else if(group=="breath-male"||group=="breath-female"||group=="breath-cancel")
            phase_sdk::Breath(group!="breath-female",group=="breath-cancel");
        else if(group=="hud-cancel")phase_sdk::HudCancel();
        else sdk_test::Require(false,"known phase SDK group");
    }
    catch(const std::exception& error){++sdk_test::failures;std::cerr<<"ERROR "<<error.what()<<'\n';}
    std::cout<<"behaviorChecks="<<kick_test::behaviorChecks<<" checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
    return sdk_test::failures?1:0;
}
