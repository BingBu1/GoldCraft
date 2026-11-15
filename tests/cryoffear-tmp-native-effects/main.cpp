// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/csol/server/weapons/guest_cof_tmp_native_effects.hpp"
#ifdef COF_TEST_SOUND_MP5
#include "native/csol/server/weapons/guest_cof_mp5_native_effects.hpp"
#endif
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
#ifdef COF_TEST_SOUND_MP5
using NativeResources=MP5Resources;
using NativeSample=MP5Sound;
constexpr const auto& NativeSamples=MP5Sounds;
constexpr auto NativeSoundEffects=MP5NativeSoundEffects;
constexpr auto EmitNativeSound=EmitMP5NativeSound;
MP5Weapon nativeWeapon;
CCSPlayerItem nativeWrapper;
#else
using NativeResources=TMPResources;
using NativeSample=TMPSound;
constexpr const auto& NativeSamples=TMPSounds;
constexpr auto NativeSoundEffects=TMPNativeSoundEffects;
constexpr auto EmitNativeSound=EmitTMPNativeSound;
auto& nativeWeapon=weapon;
#endif
std::vector<unsigned> nativeCalls;
NativeResources nativeResources;
NativeSoundAuthority soundAuthority;
int soundMutation{},soundMutateAt{},soundCurrentCalls{};
void MutateSound()
{
    switch(soundMutation)
    {
    case 1:++ownerEdict.serialnumber;break;
    case 2:++weaponEdict.serialnumber;break;
    case 3:weaponEdict.pvPrivateData=nullptr;break;
    case 4:nativeWeapon.m_pPlayer=nullptr;break;
    case 5:++nativeResources.generation;break;
    case 6:current=false;break;
    case 7:ownerEdict.free=TRUE;break;
    case 8:nativeWeapon.pev->flags|=FL_KILLME;break;
    case 9:ownerEdict.pvPrivateData=nullptr;break;
    case 10:nativeWeapon.pev=&ownerEdict.v;break;
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
    Initialize();
#ifdef COF_TEST_SOUND_MP5
    weapon.Invalidate();nativeWeapon.pev=&weaponEdict.v;weaponEdict.pvPrivateData=&nativeWeapon;
    nativeWeapon.m_pEntity=&nativeWrapper;nativeWrapper.m_pContainingEntity=&nativeWeapon;
    static char mp5Strings[]="\0models/guest/cof/mp5/v_mp5.mdl\0models/guest/cof/mp5/p_mp5.mdl\0models/guest/cof/mp5/w_mp5.mdl\0inventoryitems/guest/cof/weapons/weapon_mp5.txt\0weapon_cof_mp5\0";
    globals.pStringBase=mp5Strings;prepared.viewModelString=1;
    prepared.playerModelString=1+int(std::strlen(mp5Strings+1))+1;
    prepared.worldModelString=prepared.playerModelString+int(std::strlen(mp5Strings+prepared.playerModelString))+1;
    prepared.inventoryItemString=prepared.worldModelString+int(std::strlen(mp5Strings+prepared.worldModelString))+1;
    nativeWeapon.pev->classname=prepared.inventoryItemString+int(std::strlen(mp5Strings+prepared.inventoryItemString))+1;
    prepared.sequences.present=(std::uint64_t{1}<<20)-1;prepared.aimOffset={0,-4.25F,2};
    prepared.aimZoom=50;prepared.hostPosition=1;backends.preparedEffects=MP5RequiredEffects;frame.firstDrawn=false;
    const auto plan=RecommendedLongGunAnimationPlan(LongGun::MP5,PistolAnimationRig::Stock);std::string error;
    Check(plan&&animations.Prepare(player,model,{1,1},LongGun::MP5,*plan,_stricmp,error));
    g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
    nativeWeapon.ConfigureColdHost({nullptr,Current,
        [](void*,MP5Weapon&,TMPPrepared& out) noexcept {out=prepared;return true;},ColdOwner,
        [](void*,CBasePlayer&,MP5Weapon&,int) noexcept {std::abort();return false;}});
    nativeWeapon.Spawn();nativeWeapon.m_iPrimaryAmmoType=1;nativeWeapon.m_iClip=30;player.m_pActiveItem=&nativeWeapon;
    Check(nativeWeapon.Bind(player,prepared,backends)==TMPStatus::Ready);
#endif
    nativeWeapon.m_pPlayer=&player;nativeCalls.clear();nativeResources={};nativeResources.generation=7;
    nativeResources.sounds.fill(1);soundAuthority={nullptr,SoundCurrent,7,false,false};
    soundMutation=soundMutateAt=soundCurrentCalls=0;
    g_engfuncs.pfnEmitSound=[](edict_t* entity,int channel,const char* sample,float volume,float attenuation,int flags,int pitch)
    {
        unsigned index=0;for(;index<NativeSamples.size();++index)if(!std::strcmp(sample,NativeSamples[index].host))break;
        Check(index<NativeSamples.size());nativeCalls.insert(nativeCalls.end(),{index,entity==&ownerEdict?1u:4u,unsigned(channel),Bits(volume),Bits(attenuation),unsigned(flags),unsigned(pitch)});
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
            want[at]=sample==21?unsigned(NativeSample::EmptyClick):sample==22?unsigned(NativeSample::EmptyNoClick):
                sample==8?unsigned(NativeSample::ItemGet):sample==9?unsigned(NativeSample::WeaponGet):
                unsigned(NativeSample::Sleeve1)+sample-10;
        }
        ResetSound();nativeWeapon.m_iPlayEmptySound=int(row[3]);nativeWeapon.pev->armorvalue=Float(row[6]);
        soundAuthority.quietAudio=row[4]!=0;soundAuthority.pickupSuppressed=row[5]!=0;
        TMPEvent event{static_cast<TMPEffect>(row[0])};event.value=int(row[1]);event.other=int(row[2]);event.a=nativeWeapon.pev->armorvalue;
        unsigned rng{};
        if(event.kind==TMPEffect::Sleeve)
        {
            // Actual class owns suppression, field reset and RNG ordering.
            deployOrder.clear();deployRandom.clear();traceDeploy=true;Check(nativeWeapon.Deploy());traceDeploy=false;
            rng=unsigned(deployRandom.size());const bool sleeve=std::find(deployOrder.begin(),deployOrder.end(),unsigned(TMPEffect::Sleeve))!=deployOrder.end();
            Check(sleeve==(want[3]!=0));
        }
        const bool sleeveSuppressed=event.kind==TMPEffect::Sleeve&&!want[3];
        if(!sleeveSuppressed)
        {
            const auto result=EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,event);
            Check(result==(want[3]?NativeSoundStatus::Emitted:NativeSoundStatus::Suppressed));
        }
        std::vector<unsigned> got{unsigned(nativeWeapon.m_iPlayEmptySound),Bits(nativeWeapon.pev->armorvalue),rng,unsigned(nativeCalls.size()/7)};
        got.insert(got.end(),nativeCalls.begin(),nativeCalls.end());Check(got==want);++rows;
    }
    Check(rows==180&&!std::getline(expected,wanted));
    // Missing consumers/resources and unsupported payloads must never succeed.
    ResetSound();TMPEvent empty{TMPEffect::EmptyClick};nativeWeapon.m_iPlayEmptySound=13;
    g_engfuncs.pfnEmitSound=nullptr;Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,empty)==NativeSoundStatus::MissingEngine&&nativeWeapon.m_iPlayEmptySound==13);
    ResetSound();nativeWeapon.m_iPlayEmptySound=13;nativeResources.sounds[unsigned(NativeSample::EmptyClick)]=0;
    Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,empty)==NativeSoundStatus::MissingResource&&nativeWeapon.m_iPlayEmptySound==13&&nativeCalls.empty());
    for(auto event:{TMPEvent{TMPEffect::FireSound},TMPEvent{TMPEffect::Count},TMPEvent{static_cast<TMPEffect>(255)}})
    {ResetSound();Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,event)==NativeSoundStatus::Unsupported&&nativeCalls.empty());}
    for(int value:{-1,3})for(auto kind:{TMPEffect::Sleeve})
    {ResetSound();TMPEvent event{kind};event.value=value;event.other=100;Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,event)==NativeSoundStatus::InvalidInput&&nativeCalls.empty());}
    for(int pitch:{94,106})
    {ResetSound();TMPEvent event{TMPEffect::Sleeve};event.other=pitch;Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,event)==NativeSoundStatus::InvalidInput&&nativeCalls.empty());}
    ResetSound();soundAuthority.current=nullptr;Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    ResetSound();current=false;Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    ResetSound();soundAuthority.resourceGeneration=0;Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,empty)==NativeSoundStatus::Cancelled&&nativeCalls.empty());
    for(int mutation=1;mutation<=14;++mutation)for(int stage:{2,3,4,-1})
    {
        ResetSound();nativeWeapon.m_iPlayEmptySound=13;soundMutation=mutation;soundMutateAt=stage;
        Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,empty)==NativeSoundStatus::Cancelled);
        Check(nativeWeapon.m_iPlayEmptySound==13&&nativeCalls.size()==(stage==4||stage==-1?7:0));
    }
    Check((NativeSoundEffects&(1u<<unsigned(TMPEffect::AutomaticSound)))!=0 &&
        (NativeSoundEffects&(1u<<unsigned(TMPEffect::FireSound)))==0);
    for(bool quiet:{false,true})for(int operation=0;operation<3;++operation)
    {
        ResetSound();soundAuthority.quietAudio=quiet;
        TMPEvent automatic{TMPEffect::AutomaticSound};
        automatic.sound={operation==1?guest::cof::AutomaticSoundSample::Tail:guest::cof::AutomaticSoundSample::Fire,
            CHAN_WEAPON,operation==2?SND_STOP:0,PITCH_NORM,operation==2?0.f:quiet?.25f:1.f,operation==2?0.f:.8f};
        Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,automatic)==NativeSoundStatus::Emitted);
        const std::vector<unsigned> wanted{unsigned(operation==1?NativeSample::Tail:NativeSample::Fire),1,CHAN_WEAPON,
            Bits(automatic.sound.volume),Bits(automatic.sound.attenuation),unsigned(automatic.sound.flags),PITCH_NORM};
        Check(nativeCalls==wanted);
        nativeCalls.clear();nativeResources.sounds[unsigned(operation==1?NativeSample::Tail:NativeSample::Fire)]=0;
        Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,automatic)==NativeSoundStatus::MissingResource&&nativeCalls.empty());
    }
    for(int mutation=1;mutation<=14;++mutation)for(int stage:{1,2,3,-1})
    {
        ResetSound();soundMutation=mutation;soundMutateAt=stage;
        TMPEvent automatic{TMPEffect::AutomaticSound};
        automatic.sound={guest::cof::AutomaticSoundSample::Fire,CHAN_WEAPON,0,PITCH_NORM,1,.8f};
        Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,automatic)==NativeSoundStatus::Cancelled);
        Check(nativeCalls.size()==(stage==3||stage==-1?7:0));
    }
    ResetSound();TMPEvent obsolete{TMPEffect::FireSound};
    Check(EmitNativeSound(soundAuthority,nativeResources,player,nativeWeapon,obsolete)==NativeSoundStatus::Unsupported&&nativeCalls.empty());
    // Exact unordered original comparison must emit and retain NaN armor.
    ResetSound();nativeWeapon.pev->armorvalue=Float(0x7fc00001);deployOrder.clear();deployRandom.clear();traceDeploy=true;
    Check(nativeWeapon.Deploy());traceDeploy=false;Check(Bits(nativeWeapon.pev->armorvalue)==0x7fc00001&&deployRandom.size()==2);
    std::cout<<rows<<" original native sound cases; 56 retirement cases; "<<checks<<" actual SDK checks; 0 failures\n";
}
