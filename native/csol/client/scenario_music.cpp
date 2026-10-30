// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "scenario_music.hpp"
#include "../scenario_music.hpp"

namespace csol::client
{
namespace
{
pfnUserMsgHook previousMusic{},previousInit{};
std::string level;
std::uint32_t revision{};
bool owned{};
void SynchronizeMusic()
{
    const char* current=engine.pfnGetLevelName?engine.pfnGetLevelName():nullptr;
    const std::string_view name=current?current:"";
    if(level==name)return;
    ResetScenarioMusic();level=name;
}
int ReceiveMusic(const char*,int size,void* data)
{
    SynchronizeMusic();
    if(!data||size<0)return 1;
    const auto packet=music::Decode({static_cast<const std::byte*>(data),static_cast<std::size_t>(size)});
    if(!packet)return 1;
    const auto delta=packet->revision-revision;
    if(revision && (!delta || delta>=0x80000000u))return 1;
    revision=packet->revision;
    const bool play=packet->action==music::Action::Play||packet->action==music::Action::Loop;
    if(!play && !owned)return 1;
    const auto command=music::Command(*packet);
    if(command && engine.pfnClientCmd)
    {
        // Paths are confined to the downloaded bgm directory by the decoder;
        // neither a map keyvalue nor a network string becomes arbitrary code.
        engine.pfnClientCmd(command->c_str());
        owned=packet->action!=music::Action::Stop;
    }
    return 1;
}
int ReceiveInit(const char* name,int size,void* data)
{
    ResetScenarioMusic();
    SynchronizeMusic();
    return previousInit?previousInit(name,size,data):1;
}
void Install(const char* name,pfnUserMsgHook hook,pfnUserMsgHook& previous)
{
    const auto old=api->HookUserMsg(name,hook);
    if(old!=hook)previous=old;
    if(!old)engine.pfnHookUserMsg(name,hook);
}
}
void InitializeScenarioMusic()
{
    Install(music::MessageName,ReceiveMusic,previousMusic);
    Install("InitHUD",ReceiveInit,previousInit);
}
void ScenarioMusicFrame(){SynchronizeMusic();}
void ResetScenarioMusic()
{
    if(owned && engine.pfnClientCmd)engine.pfnClientCmd("mp3 stop\n");
    owned=false;revision=0;
}
void ShutdownScenarioMusic(){ResetScenarioMusic();level.clear();}
}
