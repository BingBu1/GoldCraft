// SPDX-License-Identifier: GPL-3.0-or-later
#define main ExistingKickFixtureMain
#include "../csol-human-kick-server/sdk.cpp"
#undef main
#include "../../native/csol/scenario/custom/challenge_cycle.hpp"
#include "../../native/csol/scenario/custom/phase_snapshot.hpp"
#include "../../native/csol/scenario/custom/environment_snapshot.hpp"
#include "../../native/csol/scenario/custom/night_lighting.hpp"
#include "../../native/csol/scenario/custom/settings.hpp"
#include "../../native/csol/server/custom/action_stamina.h"
#include "../../native/csol/server/custom/gameplay_settings.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_motion.h"
#include "../../native/csol/server/characters.h"
#include "../../native/csol/scenario/custom/stamina_audio.hpp"
#include "../../native/csol/scenario/custom/stamina_snapshot.hpp"
#include "../../native/csol/scenario/custom/phase_art.hpp"
#include "../../native/csol/scenario/core/scenario_music.hpp"
#include "goldcraft/csol_scenario_admin_api.hpp"
#include "goldcraft/csol_gameplay_settings_api.hpp"
#include "../csol-challenge-environment/provider_loader.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

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
void Settings(bool enabled, bool monsterBuffs = false, bool population = false)
{
    if(enabled)environment_test::LoadProvider();
    custom::Draft draft; custom::Error error; custom::SnapshotPtr snapshot;
    Require(draft.Set(custom::Feature::TimeCycle,enabled,error) &&
        draft.Set(custom::Setting::MiddaySeconds,2,error) &&
        draft.Set(custom::Setting::DuskSeconds,3,error) &&
        draft.Set(custom::Feature::DuskBuff,monsterBuffs,error) &&
        draft.Set(custom::Feature::NightBuff,monsterBuffs,error),
        "actual grouped settings prepare two-second noon and three-second dusk");
    if (population)
    {
        using Setting = custom::Setting;
        Require(draft.Set(custom::Feature::ZombiePopulation, true, error) &&
            draft.Set(custom::Feature::RandomHorde, true, error) &&
            draft.Set(Setting::MiddayZombieBase, 0, error) &&
            draft.Set(Setting::DuskZombieBase, 1, error) &&
            draft.Set(Setting::NightZombieBase, 2, error) &&
            draft.Set(Setting::ZombiesPerPlayer, 1, error) &&
            draft.Set(Setting::HordeIntervalSeconds, 2, error) &&
            draft.Set(Setting::HordeProbability, .5, error) &&
            draft.Set(Setting::HordeDurationSeconds, 2, error) &&
            draft.Set(Setting::HordeExtraZombies, 2, error), "configure opt-in wave limits and night horde");
    }
    Require(draft.Finish(snapshot,error), "finish the persisted gameplay settings");
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
    if (CSOL_RulesRejected()) std::cerr << "phase rejection: " << CSOL_RulesRejectionReason() << '\n';
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
        Expect(c.f.engine.messages.contains(custom::PhaseMessage),"cold registration permits a later live enable without sending a phase");
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
    c.f.engine.globals.frametime=.01f;
    for(const float time:{100.01f,101.f,102.f,103.f})
        Require(CSOL_TickActionStamina(c.f.runtime,c.player,time,error),"original strict three-second recovery wait");
    Expect(count()==0&&hudCount()==2&&latestHud().amount==0,
        "original CSOL wait does not emit a premature recovery cue or refill");
    if(cancel)c.callback=[&](std::string_view edge){if(edge=="message"&&c.packets.back().id==id)c.f.runtime->RequestClose();};
    const bool stepped=CSOL_TickActionStamina(c.f.runtime,c.player,103.01f,error);
    Require(cancel?!stepped:stepped,"real audio-send callback preserves or retires the exact stamina owner");
    Require(count()==1,"real exhausted recovery edge sends exactly one voice message");
    const auto packet=std::find_if(c.packets.rbegin(),c.packets.rend(),[&](const auto& p){return p.id==id;});
    const auto sound=custom::DecodeRecoverySound(packet->bytes);
    Expect(sound&&sound->male==male&&sound->event==1&&packet->destination==MSG_ONE&&packet->receiver==c.player.entindex(),
        "actual transport publishes one matched gender/life/event only to its owner");
    if(cancel)
    {
        Expect(!CSOL_TickActionStamina(c.f.runtime,c.player,104,error)&&count()==1,
            "retired map cannot publish later breathing");
        return;
    }
    Expect(hudCount()==3&&!latestHud().exhausted&&latestHud().amount==.001f,"HUD carries original accumulator recovery at the real recovery edge");
    for(int i=0;i<100;++i)Require(CSOL_TickActionStamina(c.f.runtime,c.player,103.01f,error),"same engine frame remains idempotent");
    Expect(hudCount()==3,"100 same-frame queries do not repeat recovery or flood the reliable channel");
    Expect(count()==1,"100 frames in the same exhausted episode never repeat the sound");
    Require(CSOL_TickActionStamina(c.f.runtime,c.player,105.02f,error),"actual original recovery delay finishes");
    Expect(!CSOL_ActionStaminaView(c.f.runtime,c.player)->exhausted&&count()==1,"recovery completion does not replay the start cue");
    Expect(hudCount()==4&&!latestHud().exhausted&&latestHud().amount>.001f,"actual accelerating recovery amount reaches HUD");
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
void LiveControls(std::string_view group)
{
    Settings(false);
    if (group != "live-missing") environment_test::LoadProvider();
    const auto resources = +[](std::map<std::string, std::string>& files)
    {
        MusicResources(files);
        files.erase("bgm/BGM_zombie_attack.mp3");
    };
    Case c(group == "live-missing-music" ? resources : MusicResources); NativeActor(c); Restart(c);
    custom::ChallengePhaseView absent;
    Expect(!CSOL_ReadChallengePhase(c.f.runtime, absent), "disabled time starts without a displayed phase");
    const auto round = c.f.session->Map().RoundGeneration();
    const auto generation = c.f.runtime->Generation();
    const auto account = c.player.m_iAccount;
    const auto weapon = c.player.m_pActiveItem;
    const auto phaseMessage = c.f.engine.messages.at(custom::PhaseMessage);
    auto latest = [&]
    {
        const auto packet = std::find_if(c.packets.rbegin(), c.packets.rend(),
            [&](const auto& p) { return p.id == phaseMessage; });
        Require(packet != c.packets.rend(), "live phase publishes through the actual message transport");
        const auto value = custom::DecodePhase(packet->bytes);
        Require(bool(value), "live phase message passes the current client decoder");
        return *value;
    };
    if (group == "live-missing" || group == "live-missing-music")
    {
        Expect(!CSOL_ScenarioTimeCycleReady(), "missing cold environment or music cannot report time capability ready");
        c.f.engine.Set("csol_custom_time_cycle", "1"); Tick(c, 120);
        Expect(!CSOL_ReadChallengePhase(c.f.runtime, absent) && !CSOL_RulesRejected(),
            "unprepared optional activation leaves the current playable map intact");
        return;
    }
    Expect(CSOL_ScenarioTimeCycleReady(), "disabled map cold-prepares the actual time/environment consumers");
    if (group == "live-cancel")
        c.callback = [&](std::string_view edge)
        { if (edge == "message" && c.packets.back().id == phaseMessage) c.f.runtime->RequestClose(); };
    c.f.engine.Set("csol_custom_time_cycle", "1");
    if (group == "live-cancel")
    {
        c.f.engine.globals.time = 120;
        CSOL_ScenarioThink(*static_cast<CHalfLifeMultiplay*>(g_pGameRules));
        Expect(c.f.runtime->Closing() && !CSOL_ReadChallengePhase(c.f.runtime, absent),
            "message callback retirement prevents a live change from continuing on an obsolete owner");
        return;
    }
    Tick(c, 120);
    const auto first = Read(c);
    Expect(first.time.phase == custom::Phase::Noon && first.time.remaining == 2 && !first.preparing,
        "CVAR enable starts noon in the running map without a round restart");
    Require(c.Update(), "live noon reaches the actual stamina owner");
    Expect(CSOL_ActionStaminaView(c.f.runtime, c.player)->infinite,
        "console enable grants noon stamina through phase authority even when saved JSON is off");
    Tick(c, 121);
    if (group == "live-invalid")
    {
        c.f.engine.Set("csol_custom_midday_seconds", "0"); Tick(c, 121);
        Expect(Read(c).time.remaining == 1 && !CSOL_RulesRejected(), "invalid CVAR duration preserves the valid active clock");
        c.f.engine.Set("csol_custom_time_cycle", "0.5"); Tick(c, 122);
        Expect(Read(c).time.phase == custom::Phase::Dusk, "invalid toggle does not stop normal progression");
        c.f.engine.Set("csol_custom_midday_seconds", "2"); c.f.engine.Set("csol_custom_time_cycle", "0"); Tick(c, 123);
        Expect(!CSOL_ReadChallengePhase(c.f.runtime, absent), "corrected CVAR control can still disable the clock");
        return;
    }
    if (group == "live-expiry")
    {
        c.f.engine.Set("csol_custom_midday_seconds", "1"); Tick(c, 121);
        Expect(Read(c).time.phase == custom::Phase::Dusk && Read(c).time.remaining == 3,
            "shortened expired noon starts a full dusk without inventing elapsed dusk time");
        c.f.engine.Set("csol_custom_dusk_seconds", "1"); Tick(c, 123);
        Expect(Read(c).time.phase == custom::Phase::Night, "shortened expired dusk enters eternal night immediately");
        return;
    }
    c.f.engine.Set("csol_custom_midday_seconds", "4"); Tick(c, 121);
    Expect(Read(c).time.phase == custom::Phase::Noon && Read(c).time.remaining == 3 && latest().remaining == 3,
        "live noon duration retains elapsed time and refreshes the actual client countdown");
    c.f.engine.Set("csol_custom_dusk_seconds", "6"); Tick(c, 124);
    Expect(Read(c).time.phase == custom::Phase::Dusk && Read(c).time.remaining == 6,
        "changed dusk duration applies at the actual phase transition");
    c.f.engine.Set("csol_custom_midday_seconds", "20"); Tick(c, 125);
    Expect(Read(c).time.phase == custom::Phase::Dusk && Read(c).time.remaining == 5,
        "editing noon while in dusk cannot roll the challenge backwards");
    c.f.engine.Set("csol_custom_dusk_seconds", "2"); Tick(c, 125);
    Expect(Read(c).time.remaining == 1, "live dusk duration retains elapsed dusk time");
    Tick(c, 126);
    Expect(Read(c).time.phase == custom::Phase::Night, "live retiming still enters persistent night");
    c.f.engine.Set("csol_custom_midday_seconds", "60"); c.f.engine.Set("csol_custom_dusk_seconds", "50"); Tick(c, 127);
    Expect(Read(c).time.phase == custom::Phase::Night, "duration edits cannot move eternal night back into an earlier phase");
    c.f.engine.Set("csol_custom_midday_seconds", "20"); Tick(c, 127);
    const auto oldChallenge = Read(c).challenge;
    c.f.engine.Set("csol_custom_time_cycle", "0"); Tick(c, 130);
    Expect(!CSOL_ReadChallengePhase(c.f.runtime, absent) && latest().phase == custom::Phase::Ended,
        "off immediately retires the authoritative phase and displayed HUD");
    Require(c.Update(), "time disable reaches stamina");
    Expect(!CSOL_ActionStaminaView(c.f.runtime, c.player)->infinite, "time disable removes the noon override");
    for (const auto* name : {custom::EnvironmentMessage, custom::NightLightingMessage})
    {
        const auto id = c.f.engine.messages.at(name);
        const auto packet = std::find_if(c.packets.rbegin(), c.packets.rend(), [&](const auto& p) { return p.id == id; });
        Require(packet != c.packets.rend(), "off publishes actual environment and player-light cleanup");
        if (!std::strcmp(name, custom::EnvironmentMessage))
        {
            const auto value = custom::DecodeEnvironment(packet->bytes);
            Require(bool(value), "environment cleanup is a valid current wire message");
            Expect(!value->view.active, "off restores map fog and light ownership");
        }
        else
        {
            const auto value = custom::DecodeNightLighting(packet->bytes);
            Require(bool(value), "player-light cleanup is a valid current wire message");
            Expect(value->phase == custom::Phase::Ended, "off clears every client's owned player lights");
        }
    }
    const auto musicId = c.f.engine.messages.at(csol::music::MessageName);
    const auto stoppedMusic = std::find_if(c.packets.rbegin(), c.packets.rend(), [&](const auto& p) { return p.id == musicId; });
    Require(stoppedMusic != c.packets.rend(), "off updates the actual music transport");
    const auto track = csol::music::Decode(stoppedMusic->bytes);
    Require(bool(track), "music cleanup is a valid current wire message");
    Expect(!track->path.ends_with(csol::music::NightFiles[0]) && !track->path.ends_with(csol::music::NightFiles[1]) &&
        !track->path.ends_with(csol::music::NightFiles[3]), "off restores map music instead of retaining the phase BGM");
    c.f.engine.Set("csol_custom_time_cycle", "1"); Tick(c, 131);
    Expect(Read(c).challenge > oldChallenge && Read(c).time.phase == custom::Phase::Noon && Read(c).time.remaining == 20,
        "reenabling creates a newer accepted cycle and uses the current CVAR duration");
    custom::PhasePresentation display;
    double received{};
    for (const auto& packet : c.packets)
        if (packet.id == phaseMessage)
            Expect(display.Receive(packet.bytes, ++received), "actual phase wire remains monotonic across off and on");
    const auto messages = std::count_if(c.packets.begin(), c.packets.end(), [&](const auto& p) { return p.id == phaseMessage; });
    c.f.engine.Set("csol_custom_time_cycle", "1"); Tick(c, 131);
    Expect(Read(c).challenge > oldChallenge &&
        std::count_if(c.packets.begin(), c.packets.end(), [&](const auto& p) { return p.id == phaseMessage; }) == messages,
        "unchanged CVAR writes do not restart the cycle or flood phase messages");
    Expect(c.f.session->Map().RoundGeneration() == round && c.f.runtime->Generation() == generation &&
        c.player.m_iAccount == account && c.player.m_pActiveItem == weapon,
        "live settings preserve map/round identity and player economy/inventory");
}
void LiveMenu()
{
    Settings(false); environment_test::LoadProvider();
    Case c(MusicResources); NativeActor(c); Restart(c);
    const auto round = c.f.session->Map().RoundGeneration();
    const auto generation = c.f.runtime->Generation();
    g_engfuncs.pfnGetPlayerUserId = [](edict_t* player)
    { return player == Case::active->player.edict() ? 71 : 0; };
    auto* settings = static_cast<csol::IGameplaySettingsService*>(
        CreateInterface(csol::GameplaySettingsServiceVersion, nullptr));
    Require(settings != nullptr, "actual GameDLL exports the settings interface used by the M-menu bridge");
    const auto edit = [&](int id, double value)
    {
        csol::GameplaySetting field;
        Require(settings->Read(id, field) && field.available && !field.nextMap,
            "actual menu service advertises immediate time controls");
        csol::GameplaySettingsStatus before, after;
        Require(settings->Status(before) && settings->Edit(1, 71, before.revision, id, value) ==
            csol::GameplayEditResult::Queued, "actual menu request queues an authorized persistent edit");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do
        {
            Require(settings->Status(after), "actual menu service polls its file worker");
            if (!after.busy && after.result != csol::GameplayEditResult::Queued) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        Require(!after.busy && after.result == csol::GameplayEditResult::Saved &&
            after.revision == before.revision + 1, "real atomic JSON save publishes the next settings revision");
        Require(settings->Read(id, field) && field.value == value && !field.nextMap,
            "menu reads the saved immediate value without a map reload");
        std::ifstream input("cstrike/addons/goldcraft/config/gameplay.json", std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input), {}};
        custom::SnapshotPtr disk; custom::Error error;
        Require(input && custom::Parse(bytes, disk, error), "sole gameplay JSON remains valid after real menu edit");
        Expect(custom::Serialize(*disk) == custom::Serialize(*CSOL_CustomSettings()),
            "disk and published menu settings agree before the running rules consume them");
    };
    const int toggle = int(custom::Feature::TimeCycle);
    const int noon = int(custom::FeatureCount) + int(custom::Setting::MiddaySeconds);
    edit(toggle, 1); Tick(c, 120);
    Expect(Read(c).time.phase == custom::Phase::Noon && Read(c).time.remaining == 2,
        "M-menu save activates the real clock in the running map");
    Tick(c, 121); edit(noon, 4); Tick(c, 121);
    Expect(Read(c).time.remaining == 3, "M-menu duration save retains already elapsed noon time");
    const auto challenge = Read(c).challenge;
    edit(toggle, 0); Tick(c, 121);
    custom::ChallengePhaseView absent;
    Expect(!CSOL_ReadChallengePhase(c.f.runtime, absent), "M-menu save disables the running phase");
    edit(noon, 6); edit(toggle, 1); Tick(c, 122);
    Expect(Read(c).time.phase == custom::Phase::Noon && Read(c).time.remaining == 6 && Read(c).challenge > challenge,
        "M-menu reenable creates a fresh cycle using its saved duration");
    Expect(c.f.session->Map().RoundGeneration() == round && c.f.runtime->Generation() == generation,
        "real menu edits never reload the map or restart its round");
}
struct MonsterDamageProbe
{
    static inline MonsterDamageProbe* active{};
    CCSOLScenarioMonster& source;
    float damage{};
    int hits{};

    explicit MonsterDamageProbe(CCSOLScenarioMonster& monster) : source(monster)
    {
        active = this;
        g_ReGameHookchains.m_CBasePlayer_TakeDamage.registerHook(Observe, 200);
    }
    ~MonsterDamageProbe()
    {
        g_ReGameHookchains.m_CBasePlayer_TakeDamage.unregisterHook(Observe);
        active = nullptr;
    }
    static BOOL Observe(IReGameHook_CBasePlayer_TakeDamage* chain, CBasePlayer* player,
        entvars_t* inflictor, entvars_t* attacker, float& amount, int flags)
    {
        if (inflictor != active->source.pev)
            return chain->callNext(player, inflictor, attacker, amount, flags);
        Expect(attacker == inflictor && flags == DMG_BULLET, "scaled melee retains the native damage source and flags");
        active->damage = amount;
        ++active->hits;
        return TRUE;
    }
};

void MonsterBuffs()
{
    Settings(true, true);
    Case c(MusicResources);
    NativeActor(c);
    Restart(c);
    auto& map = c.f.session->Map();
    Expect(!map.ActiveMonsterPhaseBuff(), "preparation exposes no monster buff");
    Tick(c, 120);
    auto& monster = c.Target(Vector(0, 0, 0));
    auto& gameplay = *c.f.runtime->gameplay;
    auto& pool = c.f.session->Monsters();
    CSOLMonsterSpawnCall call(map, pool, gameplay.Deaths().Requests(), monster);
    const auto* spawn = gameplay.Factory().State(monster);
    Require(call.Current() == &monster && spawn && spawn->attributes.attackDamage > 0,
        "actual monster pool lease carries attack attributes");
    const auto round = map.RoundGeneration();
    const float baseSpeed = monster.SpeedState().baseSpeed;
    const auto approximately = [](float a, float b) { return std::abs(a - b) < .01f; };
    const auto move = [&](float expected, bool ladder = false)
    {
        CSOLMonsterRouteMotion motion;
        motion.velocity = Vector2D(1000, 0);
        motion.traversingLadder = ladder;
        int flags{};
        const float nativeCap = monster.pev->maxspeed;
        const auto result = CSOL_CommitMonsterMotion(call, gameplay.Occupancy(),
            *csol::scenario::FindMonsterAppearance(monster.Type()->appearance),
            motion, 0, gpGlobals->time, flags);
        Expect(result == CSOLMonsterMotionResult::Applied && approximately(monster.pev->velocity.x, expected) &&
            monster.pev->maxspeed == nativeCap, "ordinary motion consumes the desired cap and preserves native AI maxspeed");
    };
    MonsterDamageProbe probe(monster);
    const auto attack = [&](float expected)
    {
        monster.pev->angles.y = 0;
        const auto playerOrigin = c.player.pev->origin;
        c.player.pev->origin = monster.pev->origin + Vector(.5f, 0, 0);
        CSOLMonsterMeleeClocks clocks;
        const float boostedUntil = gpGlobals->time + 5;
        const auto before = probe.hits;
        const auto result = CSOL_TryOrdinaryMonsterMelee(call, *spawn, EHANDLE(&c.player),
            boostedUntil, false, 0, 0, clocks);
        c.player.pev->origin = playerOrigin;
        Expect(result == CSOLMonsterMeleeResult::Hit && probe.hits == before + 1 &&
            approximately(probe.damage, spawn->attributes.attackDamage * expected + 10),
            "ordinary melee scales only base damage and adds the existing skill bonus once");
    };
    // Run the actual ordinary Think with native freeze gating locomotion and
    // attacks. Healing receives absolute engine time even while frozen.
    const auto heal = [&](float expected)
    {
        monster.HostState().frozenRemaining = 1;
        monster.Behavior()->Think(monster);
        Expect(approximately(monster.pev->health, expected), "ordinary Think heals with the current phase's elapsed time");
    };
    monster.pev->health = 40;
    monster.pev->max_health = 100;
    heal(40);
    move(baseSpeed);
    attack(1);
    c.f.engine.Set("csol_custom_dusk_speed_multiplier", "1.25");
    c.f.engine.Set("csol_custom_dusk_regen_per_second", "8");
    Tick(c, 122);
    Expect(Read(c).time.phase == custom::Phase::Dusk && map.ActiveMonsterPhaseBuff(),
        "actual phase boundary publishes configured dusk modifiers");
    heal(40);
    move(baseSpeed * 1.25f);
    move(baseSpeed, true);
    monster.pev->maxspeed = 30;
    move(30);
    monster.pev->maxspeed = baseSpeed;
    c.f.engine.globals.time = 122.25f;
    heal(42);
    heal(42);
    c.f.engine.Set("csol_custom_dusk_regen_per_second", "12");
    Tick(c, 123);
    heal(42);
    c.f.engine.globals.time = 123.5f;
    heal(48);
    attack(1);

    c.f.engine.Set("csol_custom_night_speed_multiplier", "1.5");
    c.f.engine.Set("csol_custom_night_damage_multiplier", "2");
    c.f.engine.Set("csol_custom_night_regen_per_second", "50");
    Tick(c, 125);
    Expect(Read(c).time.phase == custom::Phase::Night, "actual phase clock enters permanent night");
    heal(48);
    move(baseSpeed * 1.5f);
    attack(2);
    c.f.engine.globals.time = 126.5f;
    heal(100);
    monster.pev->health = 50;
    monster.pev->takedamage = DAMAGE_NO;
    c.f.engine.globals.time = 127;
    heal(50);
    monster.pev->takedamage = DAMAGE_AIM;
    c.f.engine.globals.time = 127.25f;
    heal(62.5f);
    c.f.engine.Set("csol_custom_night_speed_multiplier", "-1");
    Tick(c, 128);
    move(baseSpeed * 1.10f);
    c.f.engine.Set("csol_custom_night_buff", "0");
    Tick(c, 129);
    Expect(!map.ActiveMonsterPhaseBuff(), "live night toggle leaves the night clock running without buffs");
    move(baseSpeed);
    attack(1);
    c.f.engine.Set("csol_custom_night_buff", "1");
    Tick(c, 130);
    Expect(map.ActiveMonsterPhaseBuff() && Read(c).time.phase == custom::Phase::Night,
        "live night toggle reenables the buff in the same round");
    c.f.engine.Set("csol_custom_time_cycle", "0");
    Tick(c, 131);
    Expect(!map.ActiveMonsterPhaseBuff() && round == map.RoundGeneration(),
        "disabling time removes buffs without reloading the map or resetting its round");
    move(baseSpeed);
    attack(1);
}

int hordeDraws{};
float hordeDraw{.25f};
void PopulationResources(std::map<std::string, std::string>& files)
{
    MusicResources(files);
    files["maps/zbs_zs_deadend.cso"] = difficulty_test::Encrypt(
        "metadata,0\r\nunused,0\r\nthreshold,100\r\nmoney,1\r\nexp,1\r\nunused,0\r\n"
        "round,waveTypes,health,armor,speed,damage,range,interval,money,unused,music,playerSpawn\r\n"
        "0,1,0,0,0,0,0,0,0,0,0,1\r\n"
        "1,2,0,0,0,0,0,0,0,0,0,1\r\n");
    files["maps/ZBS_TRG_zs_deadend.cso"] = difficulty_test::Encrypt(
        "id,type,map trigger id,wave id,cool time,dest ratio,end wave\r\n"
        "1,w,1,1,0,0,\r\n"
        "1,g," + std::to_string(kick_world_test::WorldFixture::GoalTriggerId) + ",,0,100,\r\n"
        "2,w,2,1,0,0,\r\n");
    files["maps/zbs_wave_zs_deadend.cso"] = difficulty_test::Encrypt(
        "waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\r\n"
        "1,1,0,100,1,1,1,\r\n"
        "1,1,1,1,0,1,1,\r\n");
}

void MonsterPopulation()
{
    Settings(true, false, true);
    Case c(PopulationResources);
    NativeActor(c);
    Restart(c);
    auto* extra = c.f.engine.Reserve(2, "player");
    auto* teammate = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&extra->v));
    extra->v.flags = FL_CLIENT | FL_FAKECLIENT;
    extra->v.origin = Vector(1100, 1000, 128);
    extra->v.health = 100;
    teammate->m_iTeam = CT;
    teammate->m_iJoiningState = JOINED;
    teammate->m_bJustConnected = false;
    teammate->has_disconnected = false;
    AddEntityHashValue(&extra->v, STRING(extra->v.classname), CLASSNAME);
    g_engfuncs.pfnRandomFloat = [](float low, float high)
    {
        if (low == 0 && high == 1)
        {
            ++hordeDraws;
            return hordeDraw;
        }
        return low + (high - low) * .5f;
    };
    auto& map = c.f.session->Map();
    auto& pool = c.f.session->Monsters();
    auto& points = map.SpawnPoints().Points();
    const auto expectCount = [&](float now, std::size_t count, unsigned quota, const char* label)
    {
        Tick(c, now);
        const auto* point = points.FindMonsterPoint(TERRORIST, 1);
        Require(point != nullptr, "actual authored spawn point remains registered");
        std::cout << "population=" << pool.CategoryCount(0) << " quota=" << point->consumed
                  << " hordeDraws=" << hordeDraws << '\n';
        Expect(pool.CategoryCount(0) == count && point->consumed == quota, label);
        for (std::size_t i = 0; i < CCSOLMonsterPool::Capacity; ++i)
            if (auto* monster = pool.At(i); monster && pool.Borrowed(*monster))
                monster->pev->origin = Vector(300.0f + float(i) * 100, 200, 128);
    };
    const auto retire = [&](int amount)
    {
        for (std::size_t i = 0; i < CCSOLMonsterPool::Capacity && amount; ++i)
            if (auto* monster = pool.At(i); monster && pool.Borrowed(*monster))
            {
                Require(pool.BeginDeath(*monster, gpGlobals->time + 1, points) && pool.Free(*monster),
                    "release a native pool slot and return its spawn point quota");
                --amount;
            }
        Require(amount == 0, "enough borrowed monsters to retire");
    };
    const auto activateWave = [&](float start)
    {
        auto& waves = map.Round().Waves();
        Require(waves.ActiveWaves().empty(), "triggered round has prepared but inactive waves");
        const Vector outside = c.player.pev->origin;
        c.player.pev->origin = Vector(4, 0, 0);
        for (int i = 0; i < c.f.engine.globals.maxClients && waves.ActiveWaves().empty(); ++i)
            Tick(c, start + float(i + 1) / 1024);
        c.player.pev->origin = outside;
        Require(waves.ActiveWaves().size() == 1 && waves.ActiveWaves().front() == 1,
            "real player scan activates the authored TRG wave");
    };
    expectCount(120, 0, 0, "preparation thaw does not dispatch an early wave");
    activateWave(120);
    expectCount(120.625f, 1, 1, "noon admits one ordinary monster for one human despite a fake client");
    // A wave dispatch at .625 schedules its next check for 1.125.
    expectCount(121.25f, 2, 2, "an ordinary one-shot bypasses the limit and a throttled repeat spends no quota");
    expectCount(122, 2, 2, "dusk limit is its base plus one real player");
    extra->v.flags &= ~FL_FAKECLIENT;
    expectCount(123, 3, 3, "a second real player increases the very next wave allowance");
    teammate->m_iTeam = SPECTATOR;
    expectCount(124, 3, 3, "spectating removes the extra allowance without despawning a live monster");
    expectCount(125, 3, 3, "night starts with its ordinary allowance");
    expectCount(126, 3, 3, "first night lottery waits for its configured interval");
    Expect(hordeDraws == 0, "no probability draw exists before the first night deadline");
    expectCount(127, 4, 4, "a successful night lottery admits an extra native spawn");
    expectCount(128, 5, 5, "the horde allows native points to refill to the increased cap");
    Expect(hordeDraws == 1, "a running horde performs no second draw");
    expectCount(129, 5, 5, "horde expiry leaves the existing monsters alive");
    retire(2);
    expectCount(130, 3, 3, "expiry restores the original night cap for future spawns");
    hordeDraw = .9f;
    expectCount(131, 3, 3, "a failed roll leaves the night allowance unchanged");
    Expect(hordeDraws == 2, "the cooldown starts at horde expiry and performs one draw");
    c.f.engine.Set("csol_custom_horde_probability", "0");
    c.f.engine.Set("csol_custom_night_zombie_base", "2.5");
    expectCount(132, 3, 3, "a fractional console cap falls back to the saved whole number");
    expectCount(133, 3, 3, "zero probability suppresses the next horde");
    Expect(hordeDraws == 2, "zero probability needs no game RNG call");
    c.f.engine.Set("csol_custom_horde_probability", "1");
    expectCount(134, 3, 3, "changing probability preserves the next scheduled lottery");
    expectCount(135, 4, 4, "probability one enables the horde without waiting for a draw");
    Expect(hordeDraws == 2, "probability one needs no game RNG call");
    c.f.engine.Set("csol_custom_horde_duration_seconds", "60");
    c.f.engine.Set("csol_custom_midday_zombie_base", "0");
    c.f.engine.Set("csol_custom_dusk_zombie_base", "0");
    c.f.engine.Set("csol_custom_night_zombie_base", "0");
    c.f.engine.Set("csol_custom_zombies_per_player", "0");
    const auto challenge = Read(c).challenge;
    auto& rules = *static_cast<CHalfLifeMultiplay*>(g_pGameRules);
    const Vector outside = c.player.pev->origin;
    c.player.pev->origin = kick_world_test::WorldFixture::GoalTriggerCenter;
    for (int i = 0; i < gpGlobals->maxClients && !rules.m_bRoundTerminating; ++i)
        Tick(c, 135 + float(i + 1) / 1024);
    c.player.pev->origin = outside;
    Require(rules.m_bRoundTerminating && rules.m_flRestartRoundTime > gpGlobals->time,
        "the authored goal volume queues the next stage in GameRules");
    const float nextStage = rules.m_flRestartRoundTime;
    const auto menu = c.player.m_iMenu;
    c.player.m_iMenu = Menu_ChooseAppearance;
    Tick(c, nextStage);
    c.player.m_iMenu = menu;
    Require(map.RoundIndex() == 1 && Read(c).challenge == challenge &&
        Read(c).time.phase == custom::Phase::Night, "ordinary completion carries the same night into stage two");
    const float nextCombat = nextStage + 20;
    expectCount(nextCombat, 0, 0, "ordinary stage preparation does not dispatch waves");
    activateWave(nextCombat);
    expectCount(nextCombat + .625f, 1, 1, "a running horde survives an ordinary stage change and raises the next cap");

    auto* replay = static_cast<csol::IScenarioReplayService*>(CreateInterface(csol::ScenarioReplayServiceVersion, nullptr));
    Require(replay && replay->RequestReplay(98), "the actual replay service accepts the current map");
    const float replayStart = nextCombat + 1;
    c.player.m_iMenu = Menu_ChooseAppearance;
    Tick(c, replayStart);
    c.player.m_iMenu = menu;
    Require(map.RoundIndex() == 0 && Read(c).preparing && Read(c).challenge == challenge + 1 &&
        Read(c).time.phase == custom::Phase::Noon, "same-map replay starts a new challenge in noon preparation");
    Require(c.f.players->Prepare(c.player), "initialize the new challenge's player record after the fixture skipped respawn");
    const float replayCombat = replayStart + 20;
    expectCount(replayCombat, 0, 0, "same-map replay starts with an empty native pool");
    activateWave(replayCombat);
    expectCount(replayCombat + .625f, 0, 0, "replayed noon uses the configured cap without the previous horde");
    expectCount(replayCombat + 1.25f, 1, 1, "same-map replay independently dispatches the one-shot");
    expectCount(replayCombat + 2, 1, 1, "next challenge dusk has no inherited horde");
    expectCount(replayCombat + 3, 1, 1, "the live zero dusk cap continues to block repeated spawns");
    expectCount(replayCombat + 4, 1, 1, "dusk does not start a horde lottery");
    expectCount(replayCombat + 5, 1, 1, "next challenge night does not inherit the still-unexpired horde");
    expectCount(replayCombat + 6, 1, 1, "next challenge night waits for its own lottery");
    expectCount(replayCombat + 7, 2, 2, "next challenge starts its own horde on the new deadline");
    c.f.engine.Set("csol_custom_random_horde", "0");
    expectCount(replayCombat + 8, 2, 2, "turning the horde off immediately restores the normal night cap");
    c.f.engine.Set("csol_custom_time_cycle", "0");
    expectCount(replayCombat + 9, 3, 3, "turning time off returns the same map to its native wave limit");
}
}
namespace phase_sdk
{
void Run(std::string_view group)
{
    if(group=="enabled"||group=="disabled")Flow(group=="enabled");
    else if(group=="join")Flow(true,true);
    else if(group=="rex"||group=="rex-join")Flow(true,group=="rex-join",true);
    else if(group=="failed"||group=="replay")FailureAndReplay(group=="replay");
    else if(group=="night-failed"||group=="night-replay")FailureAndReplay(group=="night-replay",true);
    else if(group=="breath-male"||group=="breath-female"||group=="breath-cancel")
        Breath(group!="breath-female",group=="breath-cancel");
    else if(group=="hud-cancel")HudCancel();
    else if(group=="live-toggle"||group=="live-expiry"||group=="live-invalid"||group=="live-missing"||group=="live-missing-music"||group=="live-cancel")LiveControls(group);
    else if(group=="live-menu")LiveMenu();
    else if(group=="monster-buffs")MonsterBuffs();
    else if(group=="monster-population")MonsterPopulation();
    else sdk_test::Require(false,"known phase SDK group");
}
}
#ifndef CSOL_PHASE_SDK_LIBRARY
int main(int argc,char** argv)
{
    try
    {
        sdk_test::Require(argc==2,"one phase SDK group");
        phase_sdk::Run(argv[1]);
    }
    catch(const std::exception& error){++sdk_test::failures;std::cerr<<"ERROR "<<error.what()<<'\n';}
    std::cout<<"behaviorChecks="<<kick_test::behaviorChecks<<" checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
    return sdk_test::failures?1:0;
}
#endif
