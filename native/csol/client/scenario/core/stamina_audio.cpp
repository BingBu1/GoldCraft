// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "../../../scenario/custom/stamina_audio.hpp"
#include "stamina_audio.hpp"

namespace csol::client
{
namespace
{
namespace custom=scenario::custom;
pfnUserMsgHook previous{},previousInit{};
std::uint64_t life{};
std::uint32_t event{};
DWORD thread{};
int Receive(const char* name,int size,void* data)
{
    if(thread==GetCurrentThreadId()&&data&&size>=0)
    {
        const auto value=custom::DecodeRecoverySound({static_cast<const std::byte*>(data),std::size_t(size)});
        if(value&&value->life>=life&&(value->life!=life||value->event>event))
        {
            life=value->life;event=value->event;
            if(engine.pfnPlaySoundByNameAtPitch)
                engine.pfnPlaySoundByNameAtPitch(custom::RecoverySounds[value->male?0:1],1.0f,100);
        }
    }
    return previous?previous(name,size,data):1;
}
int Init(const char* name,int size,void* data)
{ResetStaminaAudio();return previousInit?previousInit(name,size,data):1;}
}
void InitializeStaminaAudio()
{
    if(thread&&thread!=GetCurrentThreadId())return;
    thread=GetCurrentThreadId();
    InstallChainedUserMessage(custom::BreathMessage,Receive,previous);
    InstallChainedUserMessage("InitHUD",Init,previousInit);
}
void ResetStaminaAudio(){if(thread==GetCurrentThreadId()){life=0;event=0;}}
}
