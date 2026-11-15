// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_tmp_native_effects.hpp"
#define main TMPCompositionMain
#include "../cryoffear-tmp-weapon/main.cpp"
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
std::vector<unsigned> nativeCalls;
TMPResources nativeResources;
NativeSoundAuthority soundAuthority;
int soundMutation{},soundMutateAt{},soundCurrentCalls{};
void MutateSound()
{
    switch(soundMutation)
    {
    case 1:++ownerEdict.serialnumber;break;
    case 2:++weaponEdict.serialnumber;break;
    case 3:weaponEdict.pvPrivateData=nullptr;break;
    case 4:weapon.m_pPlayer=nullptr;break;
    case 5:++nativeResources.generation;break;
    case 6:current=false;break;
    case 7:ownerEdict.free=TRUE;break;
    case 8:weapon.pev->flags|=FL_KILLME;break;
    case 9:ownerEdict.pvPrivateData=nullptr;break;
    case 10:weapon.pev=&ownerEdict.v;break;
    case 11:player.pev=&weaponEdict.v;break;
    case 12:weaponEdict.v.pContainingEntity=&ownerEdict;break;
    case 13:++nativeResources.generation;++soundAuthority.resourceGeneration;break;
    case 14:soundAuthority.current=nullptr;current=false;break;
    }
}
bool SoundCurrent(void*) noexcept
{
    if(++soundCurrentCalls==soundMutateAt)MutateSound();
    return current;
}
void ResetSound()
{
    Initialize();weapon.m_pPlayer=&player;nativeCalls.clear();nativeResources={};nativeResources.generation=7;
    nativeResources.sounds.fill(1);soundAuthority={nullptr,SoundCurrent,7,false,false};
    soundMutation=soundMutateAt=soundCurrentCalls=0;
    g_engfuncs.pfnEmitSound=[](edict_t* entity,int channel,const char* sample,float volume,float attenuation,int flags,int pitch)
    {
        unsigned index=0;for(;index<TMPSounds.size();++index)if(!std::strcmp(sample,TMPSounds[index].host))break;
        Check(index<TMPSounds.size());nativeCalls.insert(nativeCalls.end(),{index,entity==&ownerEdict?1u:4u,unsigned(channel),Bits(volume),Bits(attenuation),unsigned(flags),unsigned(pitch)});
        if(soundMutateAt==-1)MutateSound();
    };
}
std::vector<unsigned> ReadRow(const std::string& line)
{
    std::istringstream in(line);std::vector<unsigned> row;unsigned value;
    while(in>>std::hex>>value)row.push_back(value);Check(in.eof());return row;
}
}
int main(int argc,char** argv)
{
    if(argc!=4)return 1;std::ifstream file(argv[1],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    std::ifstream cases(argv[2]),expected(argv[3]);std::string line,wanted;unsigned rows{};
    while(std::getline(cases,line))
    {
        Check(bool(std::getline(expected,wanted)));auto row=ReadRow(line),want=ReadRow(wanted);Check(row.size()==7&&want.size()>=4);
        // Map original logical samples to the TMP namespaced catalog indices.
        for(std::size_t at=4;at<want.size();at+=7) {
            const auto sample=want[at];
            Check(sample==8||sample==9||(sample>=10&&sample<=12)||sample==21||sample==22);
            want[at]=sample==21?unsigned(TMPSound::EmptyClick):sample==22?unsigned(TMPSound::EmptyNoClick):
                sample==8?unsigned(TMPSound::ItemGet):sample==9?unsigned(TMPSound::WeaponGet):
                unsigned(TMPSound::Sleeve1)+sample-10;
        }
        ResetSound();weapon.m_iPlayEmptySound=int(row[3]);weapon.pev->armorvalue=Float(row[6]);
        soundAuthority.quietAudio=row[4]!=0;soundAuthority.pickupSuppressed=row[5]!=0;
        TMPEvent event{static_cast<TMPEffect>(row[0])};event.value=int(row[1]);event.other=int(row[2]);event.a=weapon.pev->armorvalue;
        unsigned rng{};
        if(event.kind==TMPEffect::Sleeve)
        {
            // Actual class owns suppression, field reset and RNG ordering.
            deployOrder.clear();deployRandom.clear();traceDeploy=true;Check(weapon.Deploy());traceDeploy=false;
            rng=unsigned(deployRandom.size());const bool sleeve=std::find(deployOrder.begin(),deployOrder.end(),unsigned(TMPEffect::Sleeve))!=deployOrder.end();
            Check(sleeve==(want[3]!=0));
        }
        const bool sleeveSuppressed=event.kind==TMPEffect::Sleeve&&!want[3];
        if(!sleeveSuppressed)
        {
            const auto result=EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,event);
            Check(result==(want[3]?NativeSoundStatus::Emitted:NativeSoundStatus::Suppressed));
        }
        std::vector<unsigned> got{unsigned(weapon.m_iPlayEmptySound),Bits(weapon.pev->armorvalue),rng,unsigned(nativeCalls.size()/7)};
        got.insert(got.end(),nativeCalls.begin(),nativeCalls.end());Check(got==want);++rows;
    }
    Check(rows==180&&!std::getline(expected,wanted));
    // Missing consumers/resources and unsupported payloads must never succeed.
    ResetSound();TMPEvent empty{TMPEffect::EmptyClick};weapon.m_iPlayEmptySound=13;
    g_engfuncs.pfnEmitSound=nullptr;Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::MissingEngine&&weapon.m_iPlayEmptySound==13);
    ResetSound();weapon.m_iPlayEmptySound=13;nativeResources.sounds[unsigned(TMPSound::EmptyClick)]=0;
    Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::MissingResource&&weapon.m_iPlayEmptySound==13&&nativeCalls.empty());
    for(auto event:{TMPEvent{TMPEffect::FireSound},TMPEvent{TMPEffect::Count},TMPEvent{static_cast<TMPEffect>(255)}})
    {ResetSound();Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,event)==NativeSoundStatus::Unsupported&&nativeCalls.empty());}
    for(int value:{-1,3})for(auto kind:{TMPEffect::Sleeve})
    {ResetSound();TMPEvent event{kind};event.value=value;event.other=100;Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,event)==NativeSoundStatus::InvalidInput&&nativeCalls.empty());}
    for(int pitch:{94,106})
    {ResetSound();TMPEvent event{TMPEffect::Sleeve};event.other=pitch;Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,event)==NativeSoundStatus::InvalidInput&&nativeCalls.empty());}
    ResetSound();soundAuthority.current=nullptr;Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    ResetSound();current=false;Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    ResetSound();soundAuthority.resourceGeneration=0;Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    for(int mutation=1;mutation<=14;++mutation)for(int stage:{2,3,4,-1})
    {
        ResetSound();weapon.m_iPlayEmptySound=13;soundMutation=mutation;soundMutateAt=stage;
        Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled);
        Check(weapon.m_iPlayEmptySound==13&&nativeCalls.size()==(stage==4||stage==-1?7:0));
    }
    Check((TMPNativeSoundEffects&(1u<<unsigned(TMPEffect::AutomaticSound)))!=0 &&
        (TMPNativeSoundEffects&(1u<<unsigned(TMPEffect::FireSound)))==0);
    for(bool quiet:{false,true})for(int operation=0;operation<3;++operation)
    {
        ResetSound();soundAuthority.quietAudio=quiet;
        TMPEvent automatic{TMPEffect::AutomaticSound};
        automatic.sound={operation==1?guest::cof::AutomaticSoundSample::Tail:guest::cof::AutomaticSoundSample::Fire,
            CHAN_WEAPON,operation==2?SND_STOP:0,PITCH_NORM,operation==2?0.f:quiet?.25f:1.f,operation==2?0.f:.8f};
        Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,automatic)==NativeSoundStatus::Emitted);
        const std::vector<unsigned> wanted{unsigned(operation==1?TMPSound::Tail:TMPSound::Fire),1,CHAN_WEAPON,
            Bits(automatic.sound.volume),Bits(automatic.sound.attenuation),unsigned(automatic.sound.flags),PITCH_NORM};
        Check(nativeCalls==wanted);
        nativeCalls.clear();nativeResources.sounds[unsigned(operation==1?TMPSound::Tail:TMPSound::Fire)]=0;
        Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,automatic)==NativeSoundStatus::MissingResource&&nativeCalls.empty());
    }
    for(int mutation=1;mutation<=14;++mutation)for(int stage:{1,2,3,-1})
    {
        ResetSound();soundMutation=mutation;soundMutateAt=stage;
        TMPEvent automatic{TMPEffect::AutomaticSound};
        automatic.sound={guest::cof::AutomaticSoundSample::Fire,CHAN_WEAPON,0,PITCH_NORM,1,.8f};
        Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,automatic)==NativeSoundStatus::Cancelled);
        Check(nativeCalls.size()==(stage==3||stage==-1?7:0));
    }
    ResetSound();TMPEvent obsolete{TMPEffect::FireSound};
    Check(EmitTMPNativeSound(soundAuthority,nativeResources,player,weapon,obsolete)==NativeSoundStatus::Unsupported&&nativeCalls.empty());
    // Exact unordered original comparison must emit and retain NaN armor.
    ResetSound();weapon.pev->armorvalue=Float(0x7fc00001);deployOrder.clear();deployRandom.clear();traceDeploy=true;
    Check(weapon.Deploy());traceDeploy=false;Check(Bits(weapon.pev->armorvalue)==0x7fc00001&&deployRandom.size()==2);
    std::cout<<rows<<" original native sound cases; 56 retirement cases; "<<checks<<" actual SDK checks; 0 failures\n";
}
