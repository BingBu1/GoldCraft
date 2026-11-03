// SPDX-License-Identifier: GPL-3.0-or-later
// Exact SDK runtime/collector bodies and full bomb + message TUs; controlled engine transport.
#include <limits>
int gmsgShake{111};
namespace presentation
{
using namespace bomb_test;
struct Packet { int destination{},type{};edict_t* receiver{};std::vector<unsigned char> bytes; };
std::vector<Packet> packets;
std::function<void()> onBegin,onEnd,onLookup,onOffset,onIndex;
std::array<edict_t*,33> recipients{};
std::vector<int> lookups;
int nextMessage{},offsetZero{};
void Byte(int value){packets.back().bytes.push_back(static_cast<unsigned char>(value));}
void Short(int value){Byte(value);Byte(static_cast<unsigned>(value)>>8);}
void Long(int value){for(unsigned i=0;i<4;++i)Byte(static_cast<unsigned>(value)>>(8*i));}
void Begin(int destination,int type,const float* origin,edict_t* receiver)
{Check(!origin,"reliable owner message has no origin");packets.push_back({destination,type,receiver,{}});auto cb=onBegin;if(cb)cb();}
void End(){auto cb=onEnd;if(cb)cb();}
float Real(const Packet& p,std::size_t at){float value;std::memcpy(&value,p.bytes.data()+at,4);return value;}
void Transport(Fixture& f)
{
    packets.clear();lookups.clear();onBegin={};onEnd={};onLookup={};onOffset={};onIndex={};
    nextMessage=100;offsetZero=-1;recipients.fill(nullptr);recipients[1]=f.player->edict();
    g_engfuncs.pfnRegUserMsg=[](const char*,int){return nextMessage++;};
    g_engfuncs.pfnMessageBegin=Begin;g_engfuncs.pfnMessageEnd=End;
    g_engfuncs.pfnWriteByte=Byte;g_engfuncs.pfnWriteShort=Short;g_engfuncs.pfnWriteLong=Long;
    g_engfuncs.pfnPEntityOfEntIndex=[](int slot)->edict_t*
    {lookups.push_back(slot);auto cb=onLookup;if(cb)cb();return recipients.at(slot);};
    g_engfuncs.pfnEntOffsetOfPEntity=[](const edict_t* entity)
    {auto cb=onOffset;if(cb)cb();for(int i=1;i<=32;++i)if(recipients[i]==entity)return i==offsetZero?0:i*128;return 0;};
    g_engfuncs.pfnIndexOfEdict=[](const edict_t* entity)
    {auto cb=onIndex;if(cb)cb();return entity==Fixture::current->player->edict()?1:2;};
    Require(const_cast<CCSOLScenarioMessages&>(f.session->Messages()).Register(),"register actual message service");
    f.player->pev->maxs.z=36.125f;
}
void Wire()
{
    Fixture f;Transport(f);const auto& m=f.session->Messages();
    Require(m.SendZombieBombProtection(f.player->edict(),2.125f),"overlay writer accepts original raw float");
    Check(packets.size()==1&&packets[0].destination==MSG_ONE&&packets[0].receiver==f.player->edict()&&
        packets[0].bytes==std::vector<unsigned char>({54,0,0,0,8,64,190,190,190,255}),"54 packet exact ten bytes");
    packets.clear();Require(m.SendMonsterDamageEffect(f.player->edict(),32,53,36.125f,2.125f),"attachment message writer");
    Check(packets.size()==1&&packets[0].bytes==std::vector<unsigned char>({199,32,0,53,0,128,16,66,0,0,8,64}),"199/53 exact short and two floats");
    for(float value:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    {packets.clear();Check(!m.SendZombieBombProtection(f.player->edict(),value)&&packets.empty(),"invalid overlay has no partial packet");}
    packets.clear();Check(!m.SendZombieBombProtection(nullptr,1)&&packets.empty(),"null owner rejected");
    f.player->edict()->free=1;Check(!m.SendZombieBombProtection(f.player->edict(),1)&&packets.empty(),"freed owner rejected");f.player->edict()->free=0;
    const auto complete=g_engfuncs;
    for(int missing=0;missing<4;++missing)
    {g_engfuncs=complete;if(missing==0)g_engfuncs.pfnMessageBegin=nullptr;if(missing==1)g_engfuncs.pfnMessageEnd=nullptr;
     if(missing==2)g_engfuncs.pfnWriteByte=nullptr;if(missing==3)g_engfuncs.pfnWriteLong=nullptr;
     Check(!m.SendZombieBombProtection(f.player->edict(),1)&&packets.empty(),"missing serializer cannot start packet");}
    g_engfuncs=complete;
    const auto reject=[&]{const auto before=packets.size();Check(!m.SendZombieBombProtection(f.player->edict(),1)&&
        !m.SendMonsterDamageEffect(f.player->edict(),1,53,1,1)&&packets.size()==before,"both writers reject engine callback reentry");};
    onBegin=reject;onEnd=reject;Require(m.SendZombieBombProtection(f.player->edict(),0),"zero duration cancels through actual writer");
    onBegin={};onEnd={};Check(packets.size()==1&&Real(packets[0],2)==0,"outer zero packet is complete");
}
void Coupling()
{
    Fixture f;Transport(f);f.Parts({Fixture::Part(1,100,2.125,25)});
    f.player->pev->health=0;f.player->pev->deadflag=DEAD_DEAD;
    float d=100,k=350;bool immune{};Require(f.Hit(d,k,immune),"actual prepare accepts dead player and publishes protection");
    Check(d==75&&k==262.5f&&f.seeds==1&&f.State()->zombieBombProtectionUntil==12.125,"presentation preserves part arithmetic");
    Check(packets.size()==2&&packets[0].bytes[0]==54&&packets[1].bytes[3]==53&&
        Real(packets[1],4)==36.125f&&Real(packets[1],8)==2.125f,"owner overlay precedes world attachment without alive gate");
    d=100;k=350;Require(f.Hit(d,k,immune),"active protection handles second hit");
    Check(packets.size()==2&&f.seeds==1&&d==75,"active protection does not republish or reroll");
}
void Slots()
{
    Fixture f;Transport(f);std::array<edict_t,32> pool{};std::array<CBaseEntity*,32> objects{};
    for(int i=0;i<32;++i){auto& ed=pool[i];ed.serialnumber=10+i;ed.v.pContainingEntity=&ed;
        objects[i]=new(&ed.v) CBaseEntity;objects[i]->pev=&ed.v;objects[i]->m_pEntity=nullptr;
        ed.v.health=0;ed.v.deadflag=DEAD_DEAD;recipients[i+1]=&ed;}
    csol::scenario::ConfigError error;Require(PublishProtection(*f.session,*f.player,4,f.Guard(),error),"32 registered edicts accepted regardless of alive");
    Check(packets.size()==33&&lookups.size()==32&&lookups.front()==1&&lookups.back()==32,"all maxClients slots traversed in ascending order");
    for(int i=0;i<32;++i)Check(packets[i+1].receiver==&pool[i]&&packets[i+1].bytes[3]==53,"each nonzero offset receives world attachment");
    packets.clear();lookups.clear();offsetZero=4;pool[8].free=1;recipients[15]=nullptr;
    Require(PublishProtection(*f.session,*f.player,4,f.Guard(),error),"unavailable recipients skipped");
    Check(packets.size()==30&&lookups.size()==32,"offset zero/free/missing do not truncate recipient traversal");
    packets.clear();lookups.clear();f.player->pev->rendermode=kRenderTransAlpha;
    Require(PublishProtection(*f.session,*f.player,4,f.Guard(),error),"rendermode4 skips world attachment");
    Check(packets.size()==1&&packets[0].bytes[0]==54&&lookups.empty(),"alpha-test suppresses only attachment after overlay");
    for(auto* entity:objects){auto* ed=entity->edict();entity->~CBaseEntity();std::free(ed->pvPrivateData);ed->pvPrivateData=nullptr;}
}
void Cancellation()
{
    for(int point=0;point<5;++point)
    {
        Fixture f;Transport(f);f.Parts({Fixture::Part(1)});f.Level(1);
        bool cancelled{};
        const auto cancel=[&]{if(!packets.empty()&&!cancelled){cancelled=true;f.players->PlayerSpawnStarted(f.Handle());}};
        if(point==0)onBegin=cancel;if(point==1)onEnd=cancel;if(point==2)onLookup=cancel;
        if(point==3)onOffset=cancel;if(point==4)onIndex=cancel;
        float d=100,k=350;bool immune=true;Check(!f.Hit(d,k,immune),"host callback cancellation propagates");
        Check(packets.size()==1&&packets[0].bytes.size()==10&&d==100&&k==350&&f.rolls==0&&!immune,
            "cancelled owner leaves completed overlay only, no impulse or addon continuation");
        Check(f.State()->zombieBombProtectionUntil==15,"committed original duration survives later callback cancellation");
        onBegin={};onEnd={};onLookup={};onOffset={};onIndex={};
    }
    Fixture f;Transport(f);f.Parts({Fixture::Part(1)});int reentered{};
    onBegin=[&]{if(++reentered==1){float d=100,k=350;bool immune{};Require(f.Hit(d,k,immune),"same-life reentry consumes already installed protection");Check(d==75&&k==262.5f,"reentry cannot roll or publish again");}};
    float d=100,k=350;bool immune{};Require(f.Hit(d,k,immune),"outer hit completes after same-life reentry");
    Check(packets.size()==2&&f.seeds==1&&d==75,"single installed protection and exactly two transport messages");onBegin={};
}
}
int main()
{
    try{presentation::Wire();presentation::Coupling();presentation::Slots();presentation::Cancellation();}
    catch(const std::exception& e){++bomb_test::failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"bomb presentation server checks="<<bomb_test::checks<<" failures="<<bomb_test::failures<<'\n';
    return bomb_test::failures?1:0;
}
