// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_automatic_native_sound.hpp"
#include <windows.h>
#include <new>
#define main ExistingWeaponFixtureMain
#include "../cryoffear-glock-weapon/main.cpp"
#undef main
BOOL CCSPlayerWeapon::DefaultDeploy(char*,char*,int,char*,int){std::abort();}
int CCSPlayerWeapon::DefaultReload(int,int,float){std::abort();}
bool CCSPlayerWeapon::DefaultShotgunReload(int,int,float,float,const char*,const char*){std::abort();}
void CCSPlayerWeapon::KickBack(float,float,float,float,float,float,int){std::abort();}
void CCSPlayerWeapon::SendWeaponAnim(int,int){std::abort();}
#include "sdk-on-create.inc"

namespace
{
using namespace csol::server::cof;
AutomaticSoundState automaticState;
AutomaticNativeSounds automaticResources;
NativeSoundAuthority automaticAuthority;
std::vector<unsigned> nativeAudio;
int mutation{}, mutateAt{}, guardCalls{}, guardsBeforeEmit{};
edict_t* audioEmitter{};
void* retiredPage{};
int retirePageAt{};

void RevokePage()
{
    Check(retiredPage&&VirtualFree(retiredPage,0,MEM_RELEASE));
    retiredPage=nullptr;current=false;
}

void Mutate()
{
    switch(mutation)
    {
    case 1:++ownerEdict.serialnumber;break;
    case 2:++weaponEdict.serialnumber;break;
    case 3:weaponEdict.pvPrivateData=nullptr;break;
    case 4:weapon.m_pPlayer=nullptr;break;
    case 5:++automaticResources.generation;break;
    case 6:current=false;break;
    case 7:ownerEdict.free=TRUE;break;
    case 8:weapon.pev->flags|=FL_KILLME;break;
    case 9:ownerEdict.pvPrivateData=nullptr;break;
    case 10:weapon.pev=&ownerEdict.v;break;
    case 11:player.pev=&weaponEdict.v;break;
    case 12:weaponEdict.v.pContainingEntity=&ownerEdict;break;
    }
}
bool AudioCurrent(void*) noexcept
{
    if(retirePageAt==1&&retiredPage)RevokePage();
    if(++guardCalls==mutateAt)Mutate();
    return current;
}
void ResetAudio()
{
    Initialize();weapon.m_pPlayer=&player;nativeAudio.clear();automaticState={};
    automaticResources={7,{1,2},{"guest/cof/automatic/fire.wav","guest/cof/automatic/tail.wav"}};
    automaticAuthority={nullptr,AudioCurrent,7,false,false};mutation=mutateAt=guardCalls=guardsBeforeEmit=retirePageAt=0;
    audioEmitter=&ownerEdict;
    g_engfuncs.pfnEmitSound=[](edict_t* entity,int channel,const char* sample,float volume,float attenuation,int flags,int pitch)
    {
        guardsBeforeEmit=guardCalls;
        Check(entity==audioEmitter);
        unsigned index=0;while(index<automaticResources.paths.size()&&std::strcmp(sample,automaticResources.paths[index]))++index;
        Check(index<automaticResources.paths.size());
        nativeAudio.insert(nativeAudio.end(),{index,unsigned(channel),unsigned(flags),unsigned(pitch),Bits(volume),Bits(attenuation),automaticState.playing});
        if(mutateAt==-1)Mutate();
        if(retirePageAt==2&&retiredPage)RevokePage();
    };
}
std::vector<unsigned> ReadAudioRow(const std::string& text)
{
    std::istringstream input(text);std::vector<unsigned> row;unsigned word;
    while(input>>std::hex>>word)row.push_back(word);
    Check(input.eof());return row;
}
AutomaticSoundHost ControllerHost()
{
    return {nullptr,Current,[](void*,const AutomaticSoundEvent& event) noexcept {
        return EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::Emitted;
    }};
}
}

int main(int argc,char** argv)
{
    if(argc!=4)return 1;
    std::ifstream modelFile(argv[1],std::ios::binary);if(!modelFile)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(modelFile),{}};
    model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    std::ifstream cases(argv[2]),expected(argv[3]);std::string line,wanted;unsigned rows{};
    while(std::getline(cases,line))
    {
        Check(bool(std::getline(expected,wanted)));const auto row=ReadAudioRow(line),want=ReadAudioRow(wanted);
        Check(row.size()==10&&row[0]<3&&want.size()>=2);
        ResetAudio();automaticState.playing=row[1]!=0;automaticAuthority.quietAudio=row[7]!=0;
        AutomaticSoundFrame audio{int(row[2]),int(row[3]),row[4]!=0,row[5]!=0,row[6]!=0,true,row[7]!=0,row[8]!=0,row[9]!=0};
        const auto result=row[0]==2?AdvanceAutomaticNativeSound(automaticState,audio,ControllerHost()):
                                   StartAutomaticNativeSound(automaticState,audio,ControllerHost());
        Check(result==AutomaticSoundResult::Started||result==AutomaticSoundResult::Stopped||result==AutomaticSoundResult::Unchanged);
        std::vector<unsigned> got{automaticState.playing,unsigned(nativeAudio.size()/7)};
        got.insert(got.end(),nativeAudio.begin(),nativeAudio.end());Check(got==want);++rows;
    }
    Check(rows==6144&&!std::getline(expected,wanted));
    const AutomaticSoundEvent fire{AutomaticSoundSample::Fire,1,0,100,1,.8F};
    const AutomaticSoundEvent stop{AutomaticSoundSample::Fire,1,SND_STOP,100,0,0};
    for(const auto& event:{fire,stop})
    {
        ResetAudio();
        Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::Emitted);
        const int beforeEmit=guardsBeforeEmit,totalGuards=guardCalls;
        Check(beforeEmit>0&&totalGuards>beforeEmit);
        ResetAudio();g_engfuncs.pfnEmitSound=nullptr;
        Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::MissingEngine&&nativeAudio.empty());
        ResetAudio();automaticResources.handles[0]=0;
        Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::MissingResource&&nativeAudio.empty());
        ResetAudio();automaticAuthority.current=nullptr;
        Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::Cancelled&&nativeAudio.empty());
        for(int kind=1;kind<=12;++kind)for(int at=1;at<=beforeEmit;++at)
        {
            ResetAudio();mutation=kind;mutateAt=at;
            Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::Cancelled&&nativeAudio.empty());
        }
        for(int kind=1;kind<=12;++kind)for(int at=beforeEmit+1;at<=totalGuards;++at)
        {
            ResetAudio();mutation=kind;mutateAt=at;
            Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::Cancelled&&nativeAudio.size()==7);
        }
        for(int kind=1;kind<=12;++kind)
        {
            ResetAudio();mutation=kind;mutateAt=-1;
            Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,event)==NativeSoundStatus::Cancelled&&nativeAudio.size()==7);
        }
    }
    for(unsigned field=0;field<7;++field)
    {
        ResetAudio();auto bad=fire;
        switch(field)
        {
        case 0:bad.sample=static_cast<AutomaticSoundSample>(2);break;
        case 1:bad.channel=2;break;
        case 2:bad.pitch=99;break;
        case 3:bad.flags=1;break;
        case 4:bad.volume=.5F;break;
        case 5:bad.attenuation=1;break;
        case 6:bad.flags=SND_STOP;bad.sample=AutomaticSoundSample::Tail;break;
        }
        const auto result=EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,bad);
        Check(result==(field==0?NativeSoundStatus::Unsupported:NativeSoundStatus::InvalidInput)&&nativeAudio.empty());
    }
    // Model actual map-hunk retirement: the edict page becomes inaccessible
    // inside a callback. Authority must reject before any subsequent read.
    for(int stage:{1,2})
    {
        ResetAudio();
        retiredPage=VirtualAlloc(nullptr,sizeof(edict_t),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        Check(retiredPage!=nullptr);
        auto* transient=::new(retiredPage) edict_t{};
        transient->serialnumber=17;transient->pvPrivateData=&player;
        transient->v.pContainingEntity=transient;
        player.pev=&transient->v;audioEmitter=transient;retirePageAt=stage;
        Check(EmitAutomaticNativeSound(automaticAuthority,automaticResources,player,weapon,fire)==NativeSoundStatus::Cancelled);
        Check(!retiredPage&&nativeAudio.size()==(stage==1?0u:7u));
        player.pev=&ownerEdict.v;audioEmitter=&ownerEdict;
    }
    std::cout<<"Automatic sound actual SDK controller/EmitSound: "<<rows<<" original windows; "<<checks<<" checks, zero failures; engine callback remains controlled\n";
}
