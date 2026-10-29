// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace wall_test
{
namespace { void Alert(ALERT_TYPE,const char*,...) {} }
Engine::Engine():edicts(std::make_unique<edict_t[]>(capacity)),strings(65536),destructors(capacity)
{
    Require(!active,"one wall fixture engine");active=this;gpGlobals=&globals;g_engfuncs={};
    globals.pStringBase=strings.data();globals.maxEntities=capacity;globals.time=10;globals.maxClients=32;
    for(int i=0;i<capacity;++i){edicts[i].free=TRUE;edicts[i].serialnumber=1;edicts[i].v.pContainingEntity=&edicts[i];}
    edicts[0].free=FALSE;
    g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t* ed,int32 size)->void*{
        Require(!ed->pvPrivateData&&size>0,"one positive private allocation");
        return ed->pvPrivateData=std::calloc(1,static_cast<std::size_t>(size));};
    g_engfuncs.pfnIndexOfEdict=Index;
    g_engfuncs.pfnPEntityOfEntIndex=[](int i)->edict_t*{return i>=0&&i<capacity?&active->edicts[i]:nullptr;};
    g_engfuncs.pfnEntOffsetOfPEntity=[](const edict_t* ed){return ed?Index(ed):0;};
    g_engfuncs.pfnPEntityOfEntOffset=[](int i)->edict_t*{return i>=0&&i<capacity?&active->edicts[i]:nullptr;};
    g_engfuncs.pfnAllocString=String;
    g_engfuncs.pfnCreateEntity=[](){return active->Raw("");};
    g_engfuncs.pfnFindEntityByString=Find;
    g_engfuncs.pfnRemoveEntity=[](edict_t* ed){active->Destroy(ed);};
    g_engfuncs.pfnPrecacheModel=[](const char* name){active->events.emplace_back(std::string("precache-model:")+name);return 7;};
    g_engfuncs.pfnPrecacheSound=[](const char* name){active->events.emplace_back(std::string("precache-sound:")+name);return 8;};
    g_engfuncs.pfnSetModel=[](edict_t* ed,const char*){active->Step("set-model",CBaseEntity::Instance(ed));};
    g_engfuncs.pfnRandomLong=[](int32 low,int32){return low;};
    g_engfuncs.pfnRandomFloat=[](float low,float){return low;};
    g_engfuncs.pfnMakeVectors=[](const float* angles){const double yaw=angles[1]*3.141592653589793/180;gpGlobals->v_forward=Vector(float(std::cos(yaw)),float(std::sin(yaw)),0);active->Step("make-vectors");};
    g_engfuncs.pfnAlertMessage=Alert;
    g_engfuncs.pfnMessageBegin=[](int destination,int type,const float*,edict_t* ed){
        Require(!active->inMessage,"messages do not nest");active->inMessage=true;
        active->messages.push_back({destination,type,ed?Index(ed):0,{},{}});};
    g_engfuncs.pfnWriteByte=[](int value){active->messages.back().integers.push_back(value);};
    g_engfuncs.pfnWriteShort=g_engfuncs.pfnWriteByte;
    g_engfuncs.pfnWriteLong=g_engfuncs.pfnWriteByte;
    g_engfuncs.pfnWriteCoord=[](float value){active->messages.back().coordinates.push_back(value);};
    g_engfuncs.pfnMessageEnd=[](){Require(active->inMessage,"message has begin");active->inMessage=false;active->Step("message-end");};
    GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&edicts[0].v));
}
Engine::~Engine()
{
    boundary={};for(int i=0;i<capacity;++i)if(edicts[i].pvPrivateData)Destroy(&edicts[i]);
    active=nullptr;gpGlobals=nullptr;g_engfuncs={};
}
void Engine::Step(std::string_view label,CBaseEntity* entity)
{events.emplace_back(label);const auto callback=boundary;if(callback)callback(label,entity);}
int Engine::Index(const edict_t* ed){return static_cast<int>(ed-active->edicts.get());}
int Engine::String(const char* value)
{
    auto& e=*active;const auto at=e.nextString,length=std::strlen(value)+1;
    Require(at+length<e.strings.size(),"bounded string pool");std::memcpy(e.strings.data()+at,value,length);e.nextString+=length;return static_cast<int>(at);
}
edict_t* Engine::Raw(const char* classname)
{
    Require(nextEntity<capacity,"bounded edict pool");auto* ed=&edicts[nextEntity++];
    ed->free=FALSE;ed->v.classname=String(classname);return ed;
}
void Engine::Destroy(edict_t* ed)
{
    Require(ed&&!ed->free,"raw free exactly once");auto* entity=CBaseEntity::Instance(ed);
    if(entity){entity->OnDestroy();delete entity->m_pEntity;entity->m_pEntity=nullptr;
        const auto destructor=destructors[Index(ed)];if(destructor)destructor(entity);
        std::free(ed->pvPrivateData);ed->pvPrivateData=nullptr;}
    ed->free=TRUE;++ed->serialnumber;++removes;
}
edict_t* Engine::Find(edict_t* previous,const char* field,const char* value)
{
    const int from=previous?Index(previous)+1:1;edict_t* found{};
    for(int i=from;i<active->nextEntity;++i){auto* ed=&active->edicts[i];if(ed->free)continue;
        const string_t s=std::strcmp(field,"classname")==0?ed->v.classname:ed->v.targetname;
        if(std::strcmp(STRING(s),value)==0){found=ed;break;}}
    active->Step("find",found?CBaseEntity::Instance(found):nullptr);return found;
}
void Engine::KV(CBaseEntity& entity,const char* key,const char* value)
{
    KeyValueData kv{const_cast<char*>("zbsbreak"),const_cast<char*>(key),const_cast<char*>(value),FALSE};
    entity.KeyValue(&kv);Require(kv.fHandled!=FALSE,"actual SDK or wall handles key");
}
CCSOLScenarioBreakable& Engine::Wall(const char* human,const char* monster,float health,int flags,Materials material)
{
    auto& e=New<CCSOLScenarioBreakable>("zbsbreak");e.pev->health=health;e.pev->model=String("*1");e.pev->spawnflags=flags;
    e.pev->mins=Vector(-16,-16,0);e.pev->maxs=Vector(16,16,64);e.pev->size=e.pev->maxs-e.pev->mins;
    e.pev->absmin=e.pev->mins;e.pev->absmax=e.pev->maxs;e.m_Material=material;
    if(human)KV(e,"hmdmgratio",human);if(monster)KV(e,"zbdmgratio",monster);e.Spawn();return e;
}
Sink& Engine::Target(const char* name){auto& s=New<Sink>("test_sink");s.pev->targetname=String(name);return s;}
CTriggerRelay& Engine::Relay(const char* name,const char* target,float delay,bool once)
{auto& r=New<CTriggerRelay>("trigger_relay");r.pev->targetname=String(name);r.pev->target=String(target);r.m_flDelay=delay;r.m_triggerType=USE_TOGGLE;if(once)r.pev->spawnflags|=SF_RELAY_FIREONCE;r.Spawn();return r;}
std::vector<edict_t*> Engine::Delays() const
{std::vector<edict_t*> result;for(int i=1;i<nextEntity;++i)if(!edicts[i].free&&edicts[i].pvPrivateData&&FClassnameIs(&edicts[i],"DelayedUse"))result.push_back(&edicts[i]);return result;}
void Engine::Think(edict_t* ed)
{Require(ed&&!ed->free&&ed->pvPrivateData,"live think edict");globals.time=ed->v.nextthink;CBaseEntity::Instance(ed)->Think();}
bool Rules::PlayerDamage(const EHANDLE&,float input,float ratio,float& output)
{calls.emplace_back("player");if(callback)callback("player");return accept&&csol::scenario::ResolvePlayerWallDamage(input,ratio,level,smallCap,feature,output);}
bool Rules::MonsterDamage(float input,float ratio,float& output)
{calls.emplace_back("monster");if(callback)callback("monster");return accept&&csol::scenario::ResolveMonsterWallDamage(input,ratio,monsterLevel,output);}
bool Rules::CreditPlayerDamage(const EHANDLE&,float damage)
{calls.emplace_back("credit");credited.push_back(damage);if(callback)callback("credit");return accept;}
std::shared_ptr<CSOLScenarioWallPlayer> Players::Find(const EHANDLE&)
{if(callback)callback("find");return missing?nullptr:state;}
bool Players::ModifyMoney(const EHANDLE&,int& amount)
{order.emplace_back("modify");amount+=extraMoney;if(callback)callback("modify");return moneyOk;}
bool Players::AddExperience(const EHANDLE&,int amount)
{order.emplace_back("xp");xp.push_back(amount);if(callback)callback("xp");return xpOk;}
RewardFixture::RewardFixture(Player& player,float moneyRatio,float xpRatio)
{
    // Physical rows intentionally have misleading names. Production must use
    // positions 2/3/4,column1 and difficulty columns 0x15/0x14.
    rounds.source.records.resize(5);rounds.source.records[2].fields={"xp_wrong_name","125"};
    rounds.source.records[3].fields={"damage_wrong_name","200"};rounds.source.records[4].fields={"money_wrong_name","50"};
    std::string csv="header\r\n9,arbitrary";
    for(int c=2;c<=21;++c)csv+=","+std::to_string(c==20?xpRatio:c==21?moneyRatio:99.0f);
    files.contents[difficulty_test::DefaultPath]=difficulty_test::Encrypt(csv+"\r\n");
    csol::scenario::ResourceError resource;Require(difficulty.BeginMap("wall_test",1,files.Api(),resource),"real difficulty resource load");
    players->state->player=&player;players->state->attackLevel=1;players->state->generation=1;
    csol::scenario::ConfigError error;rewards=CCSOLScenarioWallRewards::Create(rounds,difficulty,players,error);
    Require(bool(rewards),"actual wall reward rules create");
}
}
