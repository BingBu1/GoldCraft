// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include "synthetic.hpp"

extern "C" void zbstrigger(entvars_t*);
// Actual members are destroyed; SDK global Bot notifications are omitted.
CNavArea::~CNavArea() {}
namespace spawn_test
{
SpawnEngine::SpawnEngine()
{
    self=this; files=SyntheticResources();
    g_engfuncs.pfnCreateNamedEntity=CreateAny;
    g_engfuncs.pfnLoadFileForMe=Load; g_engfuncs.pfnFreeFile=FreeFile;
    g_engfuncs.pfnPEntityOfEntIndex=ClientIndex;
    g_engfuncs.pfnIsDedicatedServer=[] { return 1; };
}
SpawnEngine::~SpawnEngine()
{
    afterDispatch={}; onProxyCreate={}; onIndex={};
    Check(buffers.empty(),"all controlled engine resource buffers freed");
    for(auto* memory:buffers) delete[] static_cast<unsigned char*>(memory);
    for(auto* player:players)
    {
        auto* ed=player->edict(); player->~CBasePlayer(); std::free(player);
        if(ed->pvPrivateData==player) ed->pvPrivateData=nullptr;
        std::erase(orphanedPrivate,player);
    }
    self=nullptr;
}
unsigned char* SpawnEngine::Load(const char* name,int* size)
{
    const auto it=self->files.find(name); if(it==self->files.end()) return nullptr;
    *size=static_cast<int>(it->second.size()); auto* result=new unsigned char[*size+1];
    std::memcpy(result,it->second.data(),*size); result[*size]=0; self->buffers.insert(result); return result;
}
void SpawnEngine::FreeFile(void* memory)
{ if(!self->buffers.erase(memory)) throw std::runtime_error("foreign file free"); delete[] static_cast<unsigned char*>(memory); }
edict_t* SpawnEngine::CreateAny(int classname)
{
    if(std::strcmp(STRING(classname),"monster_entity")==0) return PoolEngine::CreatePoolEntity(classname);
    const auto* name=STRING(classname);
    if(std::strcmp(name,"playerspawn")!=0 && std::strcmp(name,"csol_playerspawn_proxy")!=0) throw std::runtime_error("unexpected proxy class");
    auto& e=*self; ++e.creates;
    auto* raw=e.Construct(name,10000+e.creates,true).edict();
    if(e.onProxyCreate) e.onProxyCreate(); return raw;
}
edict_t* SpawnEngine::ClientIndex(int index)
{ if(self->onIndex) self->onIndex(); return Engine::FromOffset(index); }
edict_t* SpawnEngine::Raw(const char* name,int index)
{
    auto ed=std::make_unique<edict_t>(); ed->serialnumber=1; ed->v.pContainingEntity=ed.get();
    auto* raw=ed.get(); indices[raw]=index; edicts.push_back(std::move(ed)); raw->v.classname=AllocString(name); return raw;
}
CBasePlayer& SpawnEngine::Client(int index,Vector origin)
{
    auto* ed=Raw("player",index); auto* p=new(&ed->v) CBasePlayer;
    p->pev=&ed->v; p->m_pEntity=nullptr; p->m_iTeam=CT;
    p->pev->origin=origin; p->pev->health=100; p->pev->deadflag=DEAD_NO;
    players.push_back(p); return *p;
}
CCSOLScenarioTrigger& SpawnEngine::Trigger(int index,int id,Vector low,Vector high)
{
    auto* ed=Raw("zbstrigger",index); ed->v.model=AllocString("*1"); zbstrigger(&ed->v);
    auto& trigger=*static_cast<CCSOLScenarioTrigger*>(ed->pvPrivateData); trigger.m_iId=id;
    trigger.Spawn(); trigger.pev->mins=low; trigger.pev->maxs=high; return trigger;
}
void SpawnEngine::Markers()
{
    Player(101,1,2); Player(102,1,2); Player(103,2,2); Monster(201,1,5,1);
    Trigger(301,1,Vector(-10,-10,-10),Vector(10,10,10));
    Trigger(302,2,Vector(90,-10,-10),Vector(110,10,10));
    Trigger(330,30,Vector(190,-10,-10),Vector(210,10,10));
}
SelectionFixture::SelectionFixture()
{
    engine.Markers(); Require(map.BeginMap("spawn_test") && map.CollectEntities() &&
        map.BeginRound(0,0,0,events),"initialize real SDK map/round/point owner");
    player=&engine.Client(1,Vector(999,999,999)); Require(selector.BeginMap(),"initialize selector deadline");
    Points({Point(0),Point(200)});
}
}
