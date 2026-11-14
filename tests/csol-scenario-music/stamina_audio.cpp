// SPDX-License-Identifier: GPL-3.0-or-later
#define main ExistingLegacyMusicClientMain
#include "client.cpp"
#undef main
#include "../../native/csol/client/scenario/core/stamina_audio.cpp"
#include "../../native/csol/scenario/custom/stamina.hpp"
#include <thread>

int main()
{
    using namespace fixture;using namespace csol::client;
    namespace custom=csol::scenario::custom;
    try
    {
        static metahook_api_t host{};api=&host;
        host.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook{
            const auto found=hooks.find(name);if(found==hooks.end())return nullptr;
            auto old=found->second;found->second=hook;return old;};
        engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook hook)->int{hooks[name]=hook;return 1;};
        engine.pfnPlaySoundByNameAtPitch=[](const char* name,float volume,int pitch){
            Check(volume==1.0f&&pitch==100);commands.emplace_back(name);};
        InitializeStaminaAudio();Check(hooks.size()==2);
        auto send=[&](custom::RecoverySound sound)
        {
            auto bytes=custom::EncodeRecoverySound(sound);Check(bytes.has_value());
            Dispatch(custom::BreathMessage,int(bytes->size()),bytes->data());
        };
        send({true,1,1});Check(commands.size()==1&&commands.back()=="vox/man/sprint_end_default.wav");
        for(int i=0;i<100;++i)send({true,1,1});Check(commands.size()==1);
        send({false,1,2});Check(commands.size()==2&&commands.back()=="vox/woman/sprint_end_default.wav");
        send({true,1,1});Check(commands.size()==2);
        send({false,2,1});Check(commands.size()==3);
        send({true,1,3});Check(commands.size()==3);
        auto packet=*custom::EncodeRecoverySound({true,3,1});
        for(std::size_t n=0;n<packet.size();++n)
        {Dispatch(custom::BreathMessage,int(n),packet.data());Check(commands.size()==3);}
        auto bad=packet;bad[0]=std::byte{2};Dispatch(custom::BreathMessage,int(bad.size()),bad.data());Check(commands.size()==3);
        bad=packet;bad[1]=std::byte{2};Dispatch(custom::BreathMessage,int(bad.size()),bad.data());Check(commands.size()==3);
        Check(!custom::EncodeRecoverySound({true,0,1})&&!custom::EncodeRecoverySound({true,1,0}));
        std::thread other([&]{Dispatch(custom::BreathMessage,int(packet.size()),packet.data());});other.join();
        Check(commands.size()==3);
        Dispatch("InitHUD",0,nullptr);send({true,1,1});Check(commands.size()==4);
        ResetStaminaAudio();send({false,1,1});Check(commands.size()==5);

        custom::StaminaState state;custom::LifeKey key{{1,1},1};
        Check(state.Begin(key,custom::Phase::Night,10));
        for(int i=0;i<10;++i)Check(state.DebitKick(key));
        custom::StaminaInput input;input.key=key;input.command=1;input.now=10;input.phase=custom::Phase::Night;
        auto value=state.Step(input);Check(value&&!value->exhausted&&value->amount==0);
        input.command=2;input.now=10.01f;
        auto before=state.View(key);value=state.Step(input);
        Check(before&&!before->exhausted&&value&&value->exhausted&&!value->infinite);
        for(int i=0;i<100;++i)
        {
            input.command++;input.now=10.02f+i*.001f;before=state.View(key);value=state.Step(input);
            Check(before&&value&&before->exhausted&&value->exhausted);
        }
        input.command++;input.now=15.02f;value=state.Step(input);Check(value&&!value->exhausted&&value->amount>1);
        custom::StaminaState noon;key.life=2;Check(noon.Begin(key,custom::Phase::Noon,10));
        for(int i=0;i<100;++i)Check(noon.DebitKick(key));
        Check(noon.View(key)->infinite&&noon.View(key)->amount==100);
        std::cout<<"PASS BGM actual stamina-audio "<<checks<<" checks; message delivery and local sound device are fixtures\n";
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
