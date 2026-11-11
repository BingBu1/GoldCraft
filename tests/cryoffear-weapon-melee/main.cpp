// SPDX-License-Identifier: GPL-3.0-or-later
// Production adapter and actual SDK classes; controlled engine/entity edges.
#include "precompiled.h"
#include "native/guest/cof/server/weapon_melee.hpp"
#include <array>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

enginefuncs_t g_engfuncs{};
globalvars_t globals{};
globalvars_t* gpGlobals=&globals;
MULTIDAMAGE gMultiDamage{};
bool g_bAllowedCSBot{};
const Vector g_vecZero{};

namespace
{
std::vector<std::uint32_t> events;
std::array<edict_t,6> edicts{};
Vector gun{}, forward{};
int mode{}, breath{}, line{}, classCalls{}, firstClass{}, secondClass{}, checks{}, cancelEvent{}, recycleEvent{}, recycleEntity{};
bool valid=true, removePrivate{}, replacePrivate{};
void* replacementPrivate{};
int cancelOccurrence=1, cancelSeen{}, precacheFail=-1;
void (*nested)(){};
std::uint32_t Bits(float f) {return std::bit_cast<std::uint32_t>(f);}
float Float(std::uint32_t i) {return std::bit_cast<float>(i);}
void Check(bool condition) {++checks;if(!condition){std::cerr<<"SDK check "<<checks<<" failed edge "<<cancelEvent<<'\n';std::exit(2);}}
int Index(const edict_t* edict) {return static_cast<int>(edict-edicts.data());}
int Index(const entvars_t* pev) {return Index(pev->pContainingEntity);}
template<class...T>void Event(std::uint32_t kind,T...v)
{
    events.insert(events.end(),{kind,sizeof...(T),static_cast<std::uint32_t>(v)...});
    if(cancelEvent==static_cast<int>(kind)&&++cancelSeen==cancelOccurrence)valid=false;
    if(recycleEvent==static_cast<int>(kind)){if(replacePrivate)edicts[recycleEntity].pvPrivateData=replacementPrivate;else if(removePrivate)edicts[recycleEntity].pvPrivateData=nullptr;else ++edicts[recycleEntity].serialnumber;recycleEvent=0;}
}
constexpr std::array<std::string_view,8> Sounds{
    "player/police/Swing/Swing1.wav","player/police/Swing/Swing2.wav","player/police/Swing/Swing3.wav",
    "player/police/Swing/Swing4.wav","player/police/Swing/Swing5.wav","player/police/Swing/Swing6.wav",
    "weapons/mobile/punch_swing.wav","weapons/mobile/punch_smack.wav"};
int SoundIndex(const char* value)
{
    for(int i=0;i<8;++i)if(Sounds[i]==value)return i;
    std::abort();
}
struct Target final:CBasePlayer
{
    int index{};
    Vector GetGunPosition() override {Event(21);return gun;}
    BOOL IsBSPModel() override {const bool bsp=mode==3||mode==4||mode==5;Event(26,index,bsp);return bsp;}
    int Classify() override {const int value=classCalls++?secondClass:firstClass;Event(25,index,value);return value;}
    void TraceAttack(entvars_t* attacker,float damage,Vector direction,TraceResult* trace,int flags) override
    {
        Event(3,index,Index(attacker),Bits(damage),Bits(direction.x),Bits(direction.y),Bits(direction.z),Bits(trace->flFraction),flags);
        gMultiDamage.hEntity=this;gMultiDamage.amount=damage;gMultiDamage.type|=flags;
        if(nested)nested();
    }
    BOOL TakeDamage(entvars_t* inflictor,entvars_t* attacker,float damage,int flags) override
    {Event(4,index,Index(inflictor),Index(attacker),Bits(damage),flags);return TRUE;}
};
std::array<Target,6> targets;
void Trace(const float* start,const float* end,edict_t* skip,TraceResult* trace,bool hull)
{
    Vector a{start},b{end};
    float fraction;
    if(hull)
    {
        Event(19,Index(skip),3,Bits(a.x),Bits(a.y),Bits(a.z),Bits(b.x),Bits(b.y),Bits(b.z));
        fraction=mode==1?1.0f:.5f;
    }
    else
    {
        ++line;Event(2,Index(skip),Bits(a.x),Bits(a.y),Bits(a.z),Bits(b.x),Bits(b.y),Bits(b.z));
        if(line==1)fraction=mode==0||mode==6?.5f:1.0f;
        else if(mode==3)fraction=.25f;
        else if(mode==4&&line>2)
        {constexpr float fractions[]{.8f,.5f,.75f,.25f,.5f,.5f,.4f,.8f};fraction=fractions[line-3];}
        else fraction=1.0f;
    }
    *trace={};trace->flFraction=fraction;
    for(int i=0;i<3;++i)trace->vecEndPos[i]=a[i]+(b[i]-a[i])*fraction;
    trace->vecPlaneNormal={0,0,1};trace->pHit=&edicts[mode==6?5:2];
}
bool Current(void*) noexcept {return valid;}
void Recoil(void*,CBasePlayer& player,const Vector& v) noexcept
{Event(24,16,Index(player.pev),29,0,Bits(v.x),Bits(v.y),Bits(v.z),Bits(v.x),Bits(v.y),Bits(v.z),0,0,0,0,0,0);}
void Initialize()
{
    globals.maxClients=3;
    g_engfuncs.pfnPrecacheSound=[](const char* name){const int index=SoundIndex(name);Event(28,index);return index==precacheFail?0:index+1;};
    for(int i=0;i<6;++i)
    {
        targets[i].index=i;targets[i].pev=&edicts[i].v;
        edicts[i].v.pContainingEntity=&edicts[i];edicts[i].pvPrivateData=&targets[i];edicts[i].serialnumber=1;
        targets[i].m_pActiveItem=reinterpret_cast<CBasePlayerItem*>(1); // Presence-only boundary, never dereferenced.
    }
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return Index(e);};
    g_engfuncs.pfnPEntityOfEntOffset=[](int){return &edicts[0];};
    g_engfuncs.pfnEntOffsetOfPEntity=[](const edict_t* e){return Index(e);};
    g_engfuncs.pfnPEntityOfEntIndex=[](int i){Event(27,i);return &edicts[i];};
    g_engfuncs.pfnRandomLong=[](int low,int high){Event(22,low,high,breath);return breath;};
    g_engfuncs.pfnEmitSound=[](edict_t* e,int channel,const char* name,float volume,float attenuation,int flags,int pitch)
    {Event(23,Index(e),channel,SoundIndex(name),Bits(volume),Bits(attenuation),flags,pitch);};
    g_engfuncs.pfnMakeVectors=[](const float* angles){Event(20,Bits(angles[0]),Bits(angles[1]),Bits(angles[2]));globals.v_forward=forward;};
    g_engfuncs.pfnTraceLine=[](const float* a,const float* b,int ignore,edict_t* skip,TraceResult* t)
    {Check(ignore==0);Trace(a,b,skip,t,false);};
    g_engfuncs.pfnTraceHull=[](const float* a,const float* b,int ignore,int hull,edict_t* skip,TraceResult* t)
    {Check(ignore==0&&hull==3);Trace(a,b,skip,t,true);};
    g_engfuncs.pfnMessageBegin=[](int dest,int id,const float*,edict_t* e){Event(6,dest,id,Index(e));};
    g_engfuncs.pfnMessageEnd=[]{Event(11);};
    g_engfuncs.pfnWriteShort=[](int value){Event(9,value);};
}
}
void ClearMultiDamage(){Event(1);gMultiDamage={};}
void ApplyMultiDamage(entvars_t* inflictor,entvars_t* attacker)
{
    Event(5,Index(inflictor),Index(attacker));
    if(auto* target=gMultiDamage.hEntity.Get<CBaseEntity>())target->TakeDamage(inflictor,attacker,gMultiDamage.amount,gMultiDamage.type);
}
void UTIL_TraceLine(const Vector& a,const Vector& b,IGNORE_MONSTERS ignore,edict_t* skip,TraceResult* t)
{g_engfuncs.pfnTraceLine(a,b,ignore,skip,t);}
void UTIL_TraceHull(const Vector& a,const Vector& b,IGNORE_MONSTERS ignore,int hull,edict_t* skip,TraceResult* t)
{g_engfuncs.pfnTraceHull(a,b,ignore,hull,skip,t);}
void UTIL_MakeVectors(const Vector& v){g_engfuncs.pfnMakeVectors(v);}
// Generated narrow definitions from actual SDK declarations. Every unreached
// out-of-line virtual aborts; these are not gameplay implementations.
#include "sdk-abort-virtuals.inc"

int main(int argc,char** argv)
{
    if(argc!=3)return 1;
    Initialize();
    std::ifstream input(argv[1]),expected(argv[2]);if(!input||!expected)return 1;
    using namespace guest::cof;
    const PistolMeleeEffects fx{78,true,Recoil,nullptr};const MeleeCall call{Current,nullptr};
    std::size_t rows{};
    while(input>>std::ws&&input.peek()!=std::char_traits<char>::eof())
    {
        std::array<std::uint32_t,22> row{};for(auto&v:row)if(!(input>>std::hex>>v))return 3;
        const auto vector=[&](int i){return Vector{Float(row[i]),Float(row[i+1]),Float(row[i+2])};};
        gun=vector(0);targets[1].pev->v_angle=vector(3);forward=vector(6);
        for(int i=1;i<=3;++i)
        {
            targets[i].pev->origin=vector(9);targets[i].pev->flags=(row[18]&(1<<(i-1)))?FL_ONGROUND:0;
            edicts[i].free=i==3&&row[20];edicts[i].pvPrivateData=i==3&&row[21]?nullptr:&targets[i];
        }
        targets[2].pev->origin.x+=Float(row[19]);edicts[5].pvPrivateData=nullptr;
        mode=row[15];breath=row[14];firstClass=row[16];secondClass=row[17];
        line=classCalls=cancelEvent=recycleEvent=0;valid=true;events.clear();
        gMultiDamage={};gMultiDamage.amount=123;gMultiDamage.type=456;
        const PistolMeleeRequest request{static_cast<int>(row[12]),row[13]!=0,MeleeWorld::Multiplayer};
        const auto result=PistolWeaponMelee(targets[1],request,fx,call);
        Check(gMultiDamage.amount==123&&gMultiDamage.type==456);
        std::string text;if(!std::getline(expected,text))return 4;std::istringstream values(text);
        std::uint32_t value;std::vector<std::uint32_t>wanted;while(values>>std::hex>>value)wanted.push_back(value);
        std::vector<std::uint32_t>actual{static_cast<std::uint32_t>(result)};actual.insert(actual.end(),events.begin(),events.end());
        if(actual!=wanted)
        {
            std::cerr<<"Original mismatch row "<<rows<<" words "<<actual.size()<<'/'<<wanted.size()<<'\n';
            for(std::size_t i=0;i<std::min(actual.size(),wanted.size());++i)if(actual[i]!=wanted[i]){std::cerr<<"word "<<i<<' '<<std::hex<<actual[i]<<" != "<<wanted[i]<<'\n';break;}
            return 5;
        }
        ++rows;
    }
    if(rows!=512)return 6;expected>>std::ws;if(expected.peek()!=std::char_traits<char>::eof())return 7;
    const PistolMeleeRequest baseline{35,false,MeleeWorld::Multiplayer};
    const auto reset=[&]
    {
        line=classCalls=cancelEvent=recycleEvent=cancelSeen=0;cancelOccurrence=1;removePrivate=replacePrivate=false;
        mode=0;firstClass=secondClass=2;breath=0;valid=true;events.clear();gun={0,0,1};forward={1,0,0};
        globals.maxClients=3;targets[1].pev->v_angle={0,0,0};
        for(int i=1;i<=3;++i){edicts[i].free=false;edicts[i].pvPrivateData=&targets[i];targets[i].pev->origin={0,0,0};targets[i].pev->flags=FL_ONGROUND;}
        edicts[3].pvPrivateData=nullptr;targets[1].m_pActiveItem=reinterpret_cast<CBasePlayerItem*>(1);
        gMultiDamage={};gMultiDamage.amount=123;gMultiDamage.type=456;
    };
    reset();Check(PrecachePistolMeleeSounds());Check(events.size()==24);
    for(int i=0;i<8;++i)Check(events[i*3]==28&&events[i*3+2]==static_cast<std::uint32_t>(i));
    events.clear();precacheFail=2;Check(!PrecachePistolMeleeSounds()&&events.size()==24);precacheFail=-1;
    // Cancellation at engine, entity, recoil and packet edges. Entered Apply
    // completes its own TakeDamage, and an entered packet always reaches End.
    constexpr std::array<std::array<int,2>,23> cancellations{{
        {22,1},{23,1},{23,2},{23,3},{24,1},{20,1},{21,1},{2,1},{2,2},{2,6},{19,1},
        {26,1},{1,1},{3,1},{5,1},{4,1},{25,1},{25,2},{27,1},{27,2},{6,1},{9,2},{11,1}}};
    for(const auto [edge,occurrence]:cancellations)
    {
        reset();mode=4;cancelEvent=edge;cancelOccurrence=occurrence;
        Check(PistolWeaponMelee(targets[1],baseline,fx,call)==MeleeStatus::Cancelled);
        Check(gMultiDamage.amount==123&&gMultiDamage.type==456);
        int packets=0,seen=0;bool after=false;
        for(std::size_t at=0;at<events.size();)
        {
            const int kind=events[at],count=events[at+1];
            if(kind==6)++packets;if(kind==11)--packets;
            if(kind==edge&&++seen==occurrence)after=true;
            else if(after&&packets==0)Check(kind==11||(edge==5&&kind==4));
            at+=2+count;
        }
        Check(seen>=occurrence&&packets==0);
    }
    for(int action:{0,1,2})
        for(const auto [edge,entity]:std::array<std::array<int,2>,4>{{{2,1},{26,2},{3,2},{4,2}}})
        {
            reset();mode=4;removePrivate=action==1;replacePrivate=action==2;replacementPrivate=&targets[4];recycleEvent=edge;recycleEntity=entity;
            Check(PistolWeaponMelee(targets[1],baseline,fx,call)==MeleeStatus::Cancelled);
            Check(gMultiDamage.amount==123&&gMultiDamage.type==456);
        }
    for(int invalid=0;invalid<10;++invalid)
    {
        reset();auto request=baseline;auto effects=fx;auto scope=call;
        switch(invalid)
        {
        case 0:request.world=MeleeWorld::UnsupportedSingleplayer;break;
        case 1:request.damage=-1;break;
        case 2:effects.soundsReady=false;break;
        case 3:effects.recoil=nullptr;break;
        case 4:effects.shakeMessage=0;break;
        case 5:scope.current=nullptr;break;
        case 6:valid=false;break;
        case 7:targets[1].m_pActiveItem=nullptr;break;
        case 8:targets[1].pev->v_angle.x=std::numeric_limits<float>::quiet_NaN();break;
        case 9:globals.maxClients=33;break;
        }
        const auto status=PistolWeaponMelee(targets[1],request,effects,scope);
        Check(status!=MeleeStatus::Hit&&status!=MeleeStatus::Miss&&status!=MeleeStatus::Ready);
        Check(events.empty()&&gMultiDamage.amount==123&&gMultiDamage.type==456);
    }
    reset();nested=[]
    {
        nested=nullptr;const auto saved=gMultiDamage;const int oldLine=line,oldCalls=classCalls;
        line=classCalls=0;
        Check(PistolWeaponMelee(targets[1],{1,true,MeleeWorld::Multiplayer},{78,true,Recoil,nullptr},{Current,nullptr})==MeleeStatus::Hit);
        Check(gMultiDamage.hEntity.Get<CBaseEntity>()==saved.hEntity.Get<CBaseEntity>()&&gMultiDamage.amount==saved.amount&&gMultiDamage.type==saved.type);
        line=oldLine;classCalls=oldCalls;
    };
    Check(PistolWeaponMelee(targets[1],baseline,fx,call)==MeleeStatus::Hit);
    Check(gMultiDamage.amount==123&&gMultiDamage.type==456);
    int applied=0;for(std::size_t at=0;at<events.size();at+=events[at+1]+2)if(events[at]==4)++applied;
    Check(applied==2);
    std::cout<<rows<<" original full-function cases; "<<checks<<" SDK checks; 0 failures\n";
}
