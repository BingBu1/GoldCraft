// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "csol/scenario_music.hpp"
#include "csol/client/scenario_music.hpp"
#include <iostream>
#include <map>
#include <source_location>
#include <cstring>

namespace fixture {
int checks{},forwards{},fallbacks{},hookLookups{},missingLookups{};bool existing=true;
std::string level="maps/zs_deadend.bsp";
std::map<std::string,pfnUserMsgHook> hooks;
std::vector<std::string> commands;
void Check(bool value,const std::source_location where=std::source_location::current())
{++checks;if(!value)throw std::runtime_error("BGM test line "+std::to_string(where.line()));}
int Previous(const char*,int,void*){++forwards;return 42;}
int Dispatch(const char* name,int size,void* data)
{
    const auto message=hooks.find(name);
    Check(message!=hooks.end());Check(message->second!=nullptr);
    return message->second(name,size,data);
}
void Send(csol::music::Action action,std::uint32_t revision,std::string path={})
{
    auto bytes=csol::music::Encode({action,revision,std::move(path)});
    Check(!bytes.empty());Check(Dispatch(csol::music::MessageName,int(bytes.size()),bytes.data())==1);
}
}
namespace csol::client { cl_enginefunc_t engine{};metahook_api_t* api{}; }
int main(int argc,char** argv)
{
    using namespace csol;using namespace csol::client;using namespace fixture;
    try {
        existing=argc==1;
        const bool registeredNull=argc==2&&!std::strcmp(argv[1],"--registered-null");
        Check(existing||registeredNull||(argc==2&&!std::strcmp(argv[1],"--no-previous")));
        if(existing||registeredNull)
        {
            hooks.emplace(music::MessageName,existing?Previous:nullptr);
            hooks.emplace("InitHUD",existing?Previous:nullptr);
        }
        static metahook_api_t host{};api=&host;
        host.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook{
            ++hookLookups;
            // MH_HookUserMsg only replaces an existing usermsg_t node. A miss
            // returns null without registering the message (metahook.cpp:498).
            const auto message=hooks.find(name);
            if(message==hooks.end()){++missingLookups;return nullptr;}
            const auto previous=message->second;message->second=hook;return previous;};
        engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook hook)->int{
            ++fallbacks;hooks.insert_or_assign(name,hook);return 1;};
        engine.pfnGetLevelName=[]()->const char*{return level.c_str();};
        engine.pfnClientCmd=[](const char* text)->int{commands.emplace_back(text);return 1;};
        InitializeScenarioMusic();ScenarioMusicFrame();Check(commands.empty());
        Check(hookLookups==2);Check(missingLookups==(!existing&&!registeredNull?2:0));
        Check(fallbacks==(existing?0:2));Check(hooks.size()==2);
        const auto musicHook=hooks.at(music::MessageName),initHook=hooks.at("InitHUD");
        Check(musicHook&&musicHook!=Previous);Check(initHook&&initHook!=Previous);
        Check(Dispatch("InitHUD",0,nullptr)==(existing?42:1));Check(forwards==(existing?1:0));
        InitializeScenarioMusic();Check(hookLookups==4);Check(fallbacks==(existing?0:2));
        Check(missingLookups==(!existing&&!registeredNull?2:0));
        Check(hooks.at(music::MessageName)==musicHook);Check(hooks.at("InitHUD")==initHook);
        Check(Dispatch("InitHUD",0,nullptr)==(existing?42:1));Check(forwards==(existing?2:0));
        Check(commands.empty());
        Send(music::Action::Fade,1);Check(commands.empty());
        Send(music::Action::Loop,2,"bgm/Scenario_Normal.mp3");Check(commands.back()=="mp3 loop \"bgm/Scenario_Normal.mp3\"\n");
        const auto first=commands.size();InitializeScenarioMusic();Check(hookLookups==6);
        Check(fallbacks==(existing?0:2));Check(commands.size()==first);
        Send(music::Action::Loop,2,"bgm/Scenario_Normal.mp3");Check(commands.size()==first);
        Send(music::Action::Loop,1,"bgm/Scenario_Rush.mp3");Check(commands.size()==first);
        Send(music::Action::Fade,3);Check(commands.back()=="cd fadeout\n");
        Send(music::Action::Play,4,"bgm/Scenario_Ready.mp3");Check(commands.back()=="mp3 play \"bgm/Scenario_Ready.mp3\"\n");
        const auto before=commands.size();ScenarioMusicFrame();Check(commands.size()==before);
        Check(Dispatch("InitHUD",0,nullptr)==(existing?42:1));Check(forwards==(existing?3:0));Check(commands.back()=="mp3 stop\n");
        Send(music::Action::Loop,4,"bgm/Scenario_Rush.mp3");Check(commands.back()=="mp3 loop \"bgm/Scenario_Rush.mp3\"\n");
        level="";ScenarioMusicFrame();Check(commands.back()=="mp3 stop\n");const auto count=commands.size();
        level="maps/cs_assault.bsp";ScenarioMusicFrame();ShutdownScenarioMusic();Check(commands.size()==count);
        level="maps/zs_deadend.bsp";Send(music::Action::Loop,0xffffffffu,"bgm/Scenario_Normal.mp3");
        Send(music::Action::Loop,1,"bgm/Scenario_Rush.mp3");Check(commands.back()=="mp3 loop \"bgm/Scenario_Rush.mp3\"\n");
        auto valid=music::Encode({music::Action::Loop,2,"bgm/Scenario_Normal.mp3"});
        for(std::size_t size=0;size<valid.size();++size){Dispatch(music::MessageName,int(size),valid.data());Check(commands.back()=="mp3 loop \"bgm/Scenario_Rush.mp3\"\n");}
        auto bad=valid;bad.push_back(0);Check(!music::Decode(std::as_bytes(std::span(bad))));
        bad=valid;bad[0]=2;Check(!music::Decode(std::as_bytes(std::span(bad))));
        bad=valid;bad[1]=255;Check(!music::Decode(std::as_bytes(std::span(bad))));
        const auto rejectedCount=commands.size();
        Dispatch(music::MessageName,7,nullptr);
        Dispatch(music::MessageName,-1,valid.data());
        Dispatch(music::MessageName,int(music::MaxPath+8),valid.data());
        Check(commands.size()==rejectedCount);
        bad=valid;bad[10]=0;Check(!music::Decode(std::as_bytes(std::span(bad))));
        bad=valid;bad[2]=bad[3]=bad[4]=bad[5]=0;Check(!music::Decode(std::as_bytes(std::span(bad))));
        Check(music::Encode({music::Action::Stop,2,"bgm/Scenario_Normal.mp3"}).empty());
        Check(music::Encode({music::Action::Loop,2,"Scenario_Normal.mp3"}).empty());
        Check(!music::ResourcePath(std::string(music::MaxPath,'x')+".mp3"));
        Check(!music::ResourcePath("folder /track.mp3"));Check(!music::ResourcePath("folder./track.mp3"));
        Check(!music::ResourcePath("bgm/./track.mp3"));Check(!music::ResourcePath("bgm//track.mp3"));
        Send(music::Action::Loop,0x80000001u,"bgm/Scenario_Normal.mp3");Check(commands.size()==rejectedCount);
        Send(music::Action::Loop,2,"bgm/Scenario_Normal.mp3");Check(commands.size()==rejectedCount+1);
        for(auto path:{"../x.mp3","/x.mp3","x;quit.mp3","x\nquit.mp3","x\".mp3","C:/x.mp3","x/../y.mp3","x//y.mp3","x/.mp3/../../y.mp3"})Check(!music::ResourcePath(path));
        Check(music::ResourcePath("folder\\track.mp3")=="bgm/folder/track.mp3");
        Check(music::ResourcePath("bgm/track.mp3")=="bgm/track.mp3");
        Check(music::FindTrack(1)->action==music::Action::Play);
        Check(music::FindTrack(5)->filename=="Scenario_Normal.mp3");
        Check(music::FindTrack(6)->filename=="Scenario_Rush.mp3");
        for(unsigned id=0;id<256;++id)Check(bool(music::FindTrack(std::uint8_t(id)))==(id<39&&id!=25));
        ShutdownScenarioMusic();Check(commands.back()=="mp3 stop\n");
        const auto stoppedCount=commands.size();ShutdownScenarioMusic();ResetScenarioMusic();Check(commands.size()==stoppedCount);
        std::cout<<"PASS BGM actual client/protocol "<<checks<<" checks; audio device not exercised\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
