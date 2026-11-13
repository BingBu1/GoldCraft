// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_revolver_native_effects.hpp"
#define main RevolverCompositionMain
#include "../cryoffear-revolver-weapon/main.cpp"
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
RevolverResources nativeResources;
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
        unsigned index=0;for(;index<RevolverSounds.size();++index)if(!std::strcmp(sample,RevolverSounds[index].host))break;
        Check(index<RevolverSounds.size());nativeCalls.insert(nativeCalls.end(),{index,entity==&ownerEdict?1u:4u,unsigned(channel),Bits(volume),Bits(attenuation),unsigned(flags),unsigned(pitch)});
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
        // Map original logical samples to the Revolver namespaced catalog indices.
        for(std::size_t at=4;at<want.size();at+=7) {
            const auto sample=want[at];
            Check(sample==8||sample==9||(sample>=10&&sample<=12)||sample==21||sample==22);
            want[at]=sample==21?unsigned(RevolverSound::EmptyClick):sample==22?unsigned(RevolverSound::EmptyNoClick):
                sample==8?unsigned(RevolverSound::ItemGet):sample==9?unsigned(RevolverSound::WeaponGet):
                unsigned(RevolverSound::Sleeve1)+sample-10;
        }
        ResetSound();weapon.m_iPlayEmptySound=int(row[3]);weapon.pev->armorvalue=Float(row[6]);
        soundAuthority.quietAudio=row[4]!=0;soundAuthority.pickupSuppressed=row[5]!=0;
        RevolverEvent event{static_cast<RevolverEffect>(row[0])};event.value=int(row[1]);event.other=int(row[2]);event.a=weapon.pev->armorvalue;
        unsigned rng{};
        if(event.kind==RevolverEffect::Sleeve)
        {
            // Actual class owns suppression, field reset and RNG ordering.
            deployOrder.clear();deployRandom.clear();traceDeploy=true;Check(weapon.Deploy());traceDeploy=false;
            rng=unsigned(deployRandom.size());const bool sleeve=std::find(deployOrder.begin(),deployOrder.end(),unsigned(RevolverEffect::Sleeve))!=deployOrder.end();
            Check(sleeve==(want[3]!=0));
        }
        const bool sleeveSuppressed=event.kind==RevolverEffect::Sleeve&&!want[3];
        if(!sleeveSuppressed)
        {
            const auto result=EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,event);
            Check(result==(want[3]?NativeSoundStatus::Emitted:NativeSoundStatus::Suppressed));
        }
        std::vector<unsigned> got{unsigned(weapon.m_iPlayEmptySound),Bits(weapon.pev->armorvalue),rng,unsigned(nativeCalls.size()/7)};
        got.insert(got.end(),nativeCalls.begin(),nativeCalls.end());Check(got==want);++rows;
    }
    Check(rows==180&&!std::getline(expected,wanted));
    // Missing consumers/resources and unsupported payloads must never succeed.
    ResetSound();RevolverEvent empty{RevolverEffect::EmptyClick};weapon.m_iPlayEmptySound=13;
    g_engfuncs.pfnEmitSound=nullptr;Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::MissingEngine&&weapon.m_iPlayEmptySound==13);
    ResetSound();weapon.m_iPlayEmptySound=13;nativeResources.sounds[unsigned(RevolverSound::EmptyClick)]=0;
    Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::MissingResource&&weapon.m_iPlayEmptySound==13&&nativeCalls.empty());
    for(auto event:{RevolverEvent{RevolverEffect::FireSound},RevolverEvent{RevolverEffect::Count},RevolverEvent{static_cast<RevolverEffect>(255)}})
    {ResetSound();Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,event)==NativeSoundStatus::Unsupported&&nativeCalls.empty());}
    for(int value:{-1,3})for(auto kind:{RevolverEffect::Sleeve})
    {ResetSound();RevolverEvent event{kind};event.value=value;event.other=100;Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,event)==NativeSoundStatus::InvalidInput&&nativeCalls.empty());}
    for(int pitch:{94,106})
    {ResetSound();RevolverEvent event{RevolverEffect::Sleeve};event.other=pitch;Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,event)==NativeSoundStatus::InvalidInput&&nativeCalls.empty());}
    ResetSound();soundAuthority.current=nullptr;Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    ResetSound();current=false;Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    ResetSound();soundAuthority.resourceGeneration=0;Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    for(int mutation=1;mutation<=14;++mutation)for(int stage:{2,3,4,-1})
    {
        ResetSound();weapon.m_iPlayEmptySound=13;soundMutation=mutation;soundMutateAt=stage;
        Check(EmitRevolverNativeSound(soundAuthority,nativeResources,player,weapon,empty)==NativeSoundStatus::Cancelled);
        Check(weapon.m_iPlayEmptySound==13&&nativeCalls.size()==(stage==4||stage==-1?7:0));
    }
    // Exact unordered original comparison must emit and retain NaN armor.
    ResetSound();weapon.pev->armorvalue=Float(0x7fc00001);deployOrder.clear();deployRandom.clear();traceDeploy=true;
    Check(weapon.Deploy());traceDeploy=false;Check(Bits(weapon.pev->armorvalue)==0x7fc00001&&deployRandom.size()==2);
    std::cout<<rows<<" original native sound cases; 56 retirement cases; "<<checks<<" actual SDK checks; 0 failures\n";
}
