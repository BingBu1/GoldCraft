// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <cstdlib>
#include <cstring>

namespace upgrade_test
{
std::shared_ptr<const UpgradeTables> RealTables()
{
    difficulty_test::Files files;
    files.contents[LevelPath]=difficulty_test::Read(resourceRoot/"ZBS_neededexp.cso");
    files.contents[ClassPath]=difficulty_test::Read(resourceRoot/"ZBS_class.cso");
    UpgradeResources resources;ResourceError error;
    const bool loaded=resources.Load(files.Api(),error);
    if(!loaded)std::cerr<<"RESOURCE "<<error.path<<" "<<error.detail.message<<'\n';
    Require(loaded,"actual encrypted upgrade tables load through production file API");
    return resources.Snapshot();
}
std::array<UpgradeAttribute,5> Attributes(int cap)
{std::array<UpgradeAttribute,5> result;result.fill({1,cap,0});return result;}
bool Events::ReadBonus(UpgradeStat stat,int& value)
{order.emplace_back("read:"+std::to_string(int(stat)));if(callback)callback("read",stat);value=bonus[std::size_t(stat)];return true;}
bool Events::BonusChanged(UpgradeStat stat,int,int)
{order.emplace_back("changed:"+std::to_string(int(stat)));if(callback)callback("changed",stat);return true;}
bool Events::LevelIncreased(UpgradeStat stat,const UpgradeProgressState&)
{increases.push_back(stat);if(callback)callback("level",stat);return true;}
namespace
{
void ResetHook(IReGameHook_CBasePlayer_ResetMaxSpeed* chain,CBasePlayer* player)
{++Engine::active->resetHooks;chain->callNext(player);}
}
Engine::Engine():edicts(std::make_unique<edict_t[]>(64)),strings(65536)
{
    Require(!active,"single upgrade engine boundary");active=this;gpGlobals=&globals;g_engfuncs={};
    globals.pStringBase=strings.data();globals.maxEntities=64;globals.maxClients=32;globals.time=10;
    for(int i=0;i<64;++i){edicts[i].free=TRUE;edicts[i].serialnumber=1;edicts[i].v.pContainingEntity=&edicts[i];}
    g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t* ed,int32 size)->void*{
        Require(!ed->pvPrivateData&&size>0,"actual SDK private allocation once");
        return ed->pvPrivateData=std::calloc(1,std::size_t(size));};
    g_engfuncs.pfnIndexOfEdict=Index;
    g_engfuncs.pfnEntOffsetOfPEntity=[](const edict_t* ed){return ed?Index(ed):0;};
    g_engfuncs.pfnPEntityOfEntIndex=[](int i)->edict_t*{return i>=0&&i<64?&active->edicts[i]:nullptr;};
    g_engfuncs.pfnPEntityOfEntOffset=g_engfuncs.pfnPEntityOfEntIndex;
    g_engfuncs.pfnAllocString=String;
    g_engfuncs.pfnRegUserMsg=[](const char* name,int size){
        Require(size==-1,"scenario messages variable size");active->registrations.emplace_back(name);
        if(std::strcmp(name,"MPToCL")==0)return 96;if(std::strcmp(name,"ZBSVer2")==0)return 97;
        throw std::runtime_error("unexpected message registration");};
    g_engfuncs.pfnMessageBegin=[](int destination,int type,const float* origin,edict_t* ed){
        Require(!active->open&&!origin,"non-nesting unpositioned packet");active->open=true;
        active->messages.push_back({destination,type,ed?Index(ed):0,{},{}});active->Step("message-begin");};
    g_engfuncs.pfnWriteByte=[](int v){Write(v,1,'B');};
    g_engfuncs.pfnWriteShort=[](int v){Write(v,2,'S');};
    g_engfuncs.pfnWriteLong=[](int v){Write(v,4,'L');};
    g_engfuncs.pfnMessageEnd=[](){Require(active->open,"matching message end");active->open=false;active->messages.back().writes+='E';active->Step("message-end");};
    rules=MakeRules();g_pGameRules=rules.get();
    g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.registerHook(ResetHook,128);
}
Engine::~Engine()
{
    boundary={};movement.reset();g_ReGameHookchains.m_CBasePlayer_ResetMaxSpeed.unregisterHook(ResetHook);
    for(int i=1;i<nextEntity;++i)if(edicts[i].pvPrivateData)Destroy(*static_cast<Player*>(edicts[i].pvPrivateData));
    g_pGameRules=nullptr;rules.reset();g_engfuncs={};gpGlobals=nullptr;active=nullptr;
}
void Engine::Step(std::string_view label)
{events.emplace_back(label);const auto f=boundary;if(f)f(label);}
int Engine::Index(const edict_t* ed){return int(ed-active->edicts.get());}
int Engine::String(const char* value)
{
    auto& e=*active;const auto at=e.nextString,n=std::strlen(value)+1;
    Require(at+n<e.strings.size(),"bounded string pool");std::memcpy(e.strings.data()+at,value,n);e.nextString+=n;return int(at);
}
void Engine::Write(int value,int bytes,char type)
{
    Require(active->open,"scalar write inside packet");auto& packet=active->messages.back();packet.writes+=type;
    for(int i=0;i<bytes;++i)packet.wire.push_back(std::uint8_t(std::uint32_t(value)>>(i*8)));
    active->Step("message-write");
}
Player& Engine::NewPlayer()
{
    Require(nextEntity<=32,"bounded player slots");auto* ed=&edicts[nextEntity++];ed->free=FALSE;
    ed->v.classname=String("player");ed->v.health=40;ed->v.max_health=100;ed->v.flags=FL_CLIENT;
    return *GetClassPtr<CCSEntity>(reinterpret_cast<Player*>(&ed->v));
}
void Engine::Destroy(Player& player)
{
    auto* ed=player.edict();Require(!ed->free&&ed->pvPrivateData,"live player release");
    player.OnDestroy();delete player.m_pEntity;player.m_pEntity=nullptr;player.~Player();
    std::free(ed->pvPrivateData);ed->pvPrivateData=nullptr;ed->free=TRUE;++ed->serialnumber;
}
void Services::Step(std::string_view label){const auto f=callback;if(f)f(label);}
bool Services::Initialize(const EHANDLE&,int id,std::array<UpgradeAttribute,5>& values)
{if(initialize)initialize(id,values);Step("initialize");return true;}
bool Services::ReadBonus(const EHANDLE&,UpgradeStat stat,int& value)
{Step("bonus");value=bonus[std::size_t(stat)];return true;}
bool Services::ModifyExperience(const EHANDLE&,int& amount)
{
    ++xpCalls;xpInputs.push_back(amount);Step("experience");
    // Controlled EXP_RATIO boundary: ordered float evaluation ends in this
    // conversion. Production must perform its own later difficulty conversion.
    amount=int(float(amount)*xpFactor);return true;
}
bool Services::MoneyAddPercent(const EHANDLE&,std::optional<float>& value)
{Step("money");value=money;return true;}
bool Services::ModifyJump(const EHANDLE&,float& value)
{++jumpCalls;Step("jump");value=gravity;return jumpOk;}
bool Services::ReachedEffectiveCap(const EHANDLE&)
{++full;Step("full");return true;}
Fixture::Fixture(float ratio):player(engine.NewPlayer()),tables(RealTables())
{
    Require(messages.Register(),"production registers both messages");
    state=CCSOLScenarioPlayerUpgrades::Create(player,tables,services,messages,ratio,1);
    Require(bool(state),"actual SDK player upgrade binding");engine.movement=state;
}
void Fixture::Configure(int id)
{Require(state->Configure(id,0,false),"configure player through production adapter");engine.messages.clear();}
}
