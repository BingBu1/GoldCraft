// SPDX-License-Identifier: GPL-3.0-or-later
#include "csol/server/scenario_music.h"
#include <iostream>
#include <functional>
#include <source_location>
#include <map>
#include <limits>

constexpr int FL_FAKECLIENT=1,FL_DORMANT=2,FL_KILLME=4,MSG_ONE=1,MSG_ALL=2;
struct Vars {int flags{};};
class CBasePlayer {public:bool has_disconnected{};Vars vars{},*pev=&vars;};
namespace fixture {
int checks{},registrations{},sizes{},precaches{},destination{},registeredMessage=64;
int begins{},ends{},depth{},maxDepth{};
bool current=true,loading=true,missing{};std::function<void()> onSize,onPrecache,onBegin,onByte,onEnd;
std::string precacheArgumentAfterCallback;
std::vector<std::string> paths;std::vector<csol::music::Packet> packets;std::vector<std::uint8_t> bytes;
void Check(bool ok,const std::source_location where=std::source_location::current())
{++checks;if(!ok)throw std::runtime_error("BGM server line "+std::to_string(where.line()));}
int Size(const char*){++sizes;if(onSize)onSize();return missing?0:500;}
int Precache(char* path){++precaches;paths.emplace_back(path);if(onPrecache)onPrecache();precacheArgumentAfterCallback=path;return precaches;}
int Register(const char* name,int length){Check(name==std::string(csol::music::MessageName)&&length==-1);++registrations;return registeredMessage;}
void Invoke(std::function<void()>& callback){if(callback){auto once=std::move(callback);callback={};once();}}
void Begin(int dest,int message,const void* =nullptr,const void* =nullptr)
{Check(message>=64&&message<=255);++begins;++depth;maxDepth=std::max(maxDepth,depth);destination=dest;bytes.clear();Invoke(onBegin);}
void Byte(int v){Check(depth==1);bytes.push_back(std::uint8_t(v));Invoke(onByte);}
void End(){Check(depth==1);auto parsed=csol::music::Decode(std::as_bytes(std::span(bytes)));Check(bool(parsed));packets.push_back(*parsed);++ends;--depth;Invoke(onEnd);}
}
struct Engine {
decltype(&fixture::Size) pfnGetFileSize=fixture::Size;
decltype(&fixture::Precache) pfnPrecacheGeneric=fixture::Precache;
decltype(&fixture::Register) pfnRegUserMsg=fixture::Register;
decltype(&fixture::Begin) pfnMessageBegin=fixture::Begin;
decltype(&fixture::Byte) pfnWriteByte=fixture::Byte;
decltype(&fixture::End) pfnMessageEnd=fixture::End;
}g_engfuncs;
void DispatchBegin(int destination,int message,const void* origin=nullptr,const void* recipient=nullptr)
{g_engfuncs.pfnMessageBegin(destination,message,origin,recipient);}
#define PRECACHE_GENERIC(p) g_engfuncs.pfnPrecacheGeneric(p)
#define REG_USER_MSG(n,s) g_engfuncs.pfnRegUserMsg(n,s)
#define MESSAGE_BEGIN(...) DispatchBegin(__VA_ARGS__)
#define WRITE_BYTE(v) g_engfuncs.pfnWriteByte(v)
#define MESSAGE_END() g_engfuncs.pfnMessageEnd()
bool CCSOLScenarioMusic::Current()const{return fixture::current&&m_access&&m_access->Allowed();}
bool CSOL_ScenarioMusicPrecacheAllowed(const CSOLScenarioEntityAccess*){return fixture::loading;}
#include "actual_server.inc"
int main()
{
    using namespace fixture;using namespace csol;using namespace csol::scenario;
    try{
        auto access=std::make_shared<CSOLScenarioEntityAccess>();CCSOLScenarioMusic music(access);
        RoundTable rounds;rounds.headerRecord=6;rounds.source.records.resize(9);rounds.rows.resize(2);
        for(int i=0;i<2;++i){rounds.rows[i].recordIndex=i+7;rounds.source.records[i+7].fields.resize(12);rounds.source.records[i+7].fields[10]=i?"6":"5";}
        ResourceError error;Check(music.Precache(rounds,error));Check(paths==std::vector<std::string>({"bgm/Scenario_Ready.mp3","bgm/Scenario_Normal.mp3","bgm/Scenario_Rush.mp3"}));
        CBasePlayer p;Check(music.Replay(p));Check(packets.empty());
        loading=false;Check(music.Select(1));Check(packets.back().action==music::Action::Play&&packets.back().revision==1);
        Check(music.Select(5));Check(packets.back().path=="bgm/Scenario_Normal.mp3"&&packets.back().action==music::Action::Loop);
        Check(music.Replay(p));Check(destination==MSG_ONE&&packets.back().revision==2);
        const auto count=packets.size();p.vars.flags=FL_FAKECLIENT;Check(music.Replay(p)&&packets.size()==count);p.vars.flags=0;
        p.has_disconnected=true;Check(music.Replay(p)&&packets.size()==count);p.has_disconnected=false;
        Check(!music.Play("missing.mp3",true)&&packets.size()==count);
        Check(music.Select(6));Check(packets.back().path=="bgm/Scenario_Rush.mp3");
        Check(music.Fade());Check(packets.back().action==music::Action::Fade&&packets.back().path.empty());
        Check(!music.PrecacheFile("late.mp3",error)&&!error.detail.message.empty());
        Check(music.PrecacheFile("Scenario_Rush.mp3",error));
        music.Reset();Check(packets.back().action==music::Action::Stop);Check(!music.Play("Scenario_Normal.mp3",true));
        loading=true;missing=true;Check(!music.Precache(rounds,error)&&error.path=="bgm/Scenario_Ready.mp3");missing=false;
        const auto before=precaches;onSize=[&](){current=false;};Check(!music.PrecacheFile("cancel.mp3",error));Check(precaches==before);onSize={};current=true;
        auto revoked=std::make_shared<CSOLScenarioEntityAccess>();CCSOLScenarioMusic retired(revoked);
        onPrecache=[&](){revoked->Revoke();};Check(!retired.PrecacheFile("retired.mp3",error));onPrecache={};
        Check(!retired.Play("retired.mp3",true));Check(!retired.Fade());
        rounds.source.records[8].fields[10]="25";Check(!music.Precache(rounds,error));Check(error.detail.message.find("25")!=std::string::npos);

        // Registration is a GoldSrc user-message byte, never a service opcode.
        rounds.source.records[8].fields[10]="6";
        for(int message:{-1,0,1,63,256,std::numeric_limits<int>::max()})
        {
            registeredMessage=message;
            CCSOLScenarioMusic rejectedMessage(std::make_shared<CSOLScenarioEntityAccess>());
            const auto previousBegins=begins;
            Check(!rejectedMessage.Precache(rounds,error));Check(!error.detail.message.empty());
            Check(!rejectedMessage.Select(1));Check(begins==previousBegins);
        }
        for(int message:{64,255})
        {
            registeredMessage=message;
            CCSOLScenarioMusic acceptedMessage(std::make_shared<CSOLScenarioEntityAccess>());
            Check(acceptedMessage.Precache(rounds,error));Check(acceptedMessage.Select(1));
        }
        registeredMessage=64;
        CCSOLScenarioMusic transport(std::make_shared<CSOLScenarioEntityAccess>());
        Check(transport.Precache(rounds,error));Check(transport.Select(5));
        const auto transportRevision=transport.Revision();const auto transportPackets=packets.size();
        g_engfuncs.pfnMessageBegin=nullptr;Check(!transport.Select(6));g_engfuncs.pfnMessageBegin=Begin;
        g_engfuncs.pfnWriteByte=nullptr;Check(!transport.Select(6));g_engfuncs.pfnWriteByte=Byte;
        g_engfuncs.pfnMessageEnd=nullptr;Check(!transport.Select(6));g_engfuncs.pfnMessageEnd=End;
        Check(packets.size()==transportPackets);Check(transport.Revision()==transportRevision);

        // Begin/WriteByte hooks must never enter a nested message or replace
        // the committed selection while the first packet is incomplete.
        bool nestedPublish=true,nestedReplay=true;
        onBegin=[&]{nestedPublish=transport.Select(6);nestedReplay=transport.Replay(p);};
        Check(transport.Select(1));Check(!nestedPublish&&!nestedReplay);
        Check(maxDepth==1&&depth==0&&begins==ends);
        Check(packets.back().path=="bgm/Scenario_Ready.mp3");
        onByte=[&]{nestedPublish=transport.Fade();};
        Check(transport.Select(5));Check(!nestedPublish);
        Check(packets.back().path=="bgm/Scenario_Normal.mp3");

        // Reset during a transport callback cancels ownership but cannot omit
        // MESSAGE_END from the already-open packet or publish a nested Stop.
        const auto beforeReset=packets.size();
        onByte=[&]{transport.Reset();};
        Check(!transport.Select(6));Check(transport.Revision()==0);
        Check(depth==0&&begins==ends&&maxDepth==1);Check(packets.size()==beforeReset+1);
        Check(!transport.Select(1));
        Check(transport.Precache(rounds,error));Check(transport.Select(5));
        onEnd=[&]{transport.Reset();};
        Check(!transport.Select(6));Check(transport.Revision()==0);Check(depth==0&&begins==ends);

        auto cancelAccess=std::make_shared<CSOLScenarioEntityAccess>();
        CCSOLScenarioMusic cancel(cancelAccess);Check(cancel.Precache(rounds,error));
        onBegin=[&]{cancelAccess->Revoke();};
        Check(!cancel.Select(1));Check(depth==0&&begins==ends);Check(!cancel.Fade());

        CCSOLScenarioMusic cancelledPrecache(std::make_shared<CSOLScenarioEntityAccess>());
        onPrecache=[&]{cancelledPrecache.Reset();};
        Check(!cancelledPrecache.PrecacheFile("cancelled.mp3",error));onPrecache={};
        Check(precacheArgumentAfterCallback=="bgm/cancelled.mp3");
        Check(!cancelledPrecache.Select(1));
        std::cout<<"PASS BGM actual server "<<checks<<" checks; SDK authority and transport edges are fixtures\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
