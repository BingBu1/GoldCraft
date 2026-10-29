// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine_fixture.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>

extern "C" void playerspawn(entvars_t*);
extern "C" void csol_playerspawn_proxy(entvars_t*);
extern "C" void zombiespawn(entvars_t*);
Engine* Engine::active{};

Engine::Engine()
{
    if (active) throw std::runtime_error("nested fixture engine");
    active=this; gpGlobals=&globals; globals.pStringBase=strings.data();g_engfuncs={};
    g_engfuncs.pfnPvAllocEntPrivateData=Allocate;
    g_engfuncs.pfnIndexOfEdict=Index;
    g_engfuncs.pfnEntOffsetOfPEntity=Offset;
    g_engfuncs.pfnPEntityOfEntOffset=FromOffset;
    g_engfuncs.pfnAllocString=AllocString;
    g_engfuncs.pfnCreateNamedEntity=Create;
    g_engfuncs.pfnSetOrigin=SetOrigin;
}
Engine::~Engine() { ClearEdicts();active=nullptr;gpGlobals=nullptr;g_engfuncs={}; }
void Engine::ClearEdicts()
{
    auto release=[](void* memory) {if(memory) {auto* e=static_cast<CBaseEntity*>(memory);delete e->m_pEntity;std::free(memory);}};
    for(auto& ed:edicts) release(ed->pvPrivateData);
    for(void* memory:orphanedPrivate) release(memory);
    orphanedPrivate.clear();edicts.clear();indices.clear();
    namedPending.clear();namedRegistrations.clear();spawnCalls.clear();
}
void* Engine::Allocate(edict_t* ed,int32 size)
{
    if(ed->pvPrivateData || size<=0) throw std::runtime_error("unexpected allocation");
    return ed->pvPrivateData=std::calloc(1,static_cast<std::size_t>(size));
}
int Engine::Index(const edict_t* ed) {return active->indices.at(ed);}
int Engine::Offset(const edict_t* ed) {return ed ? Index(ed):0;}
edict_t* Engine::FromOffset(int index)
{
    for(auto& [ed,id]:active->indices) if(id==index) return const_cast<edict_t*>(ed);
    return nullptr;
}
int Engine::AllocString(const char* text)
{
    const auto length=std::strlen(text)+1;const auto offset=active->nextString;
    if(offset+length>active->strings.size()) throw std::runtime_error("fixture string pool full");
    std::memcpy(active->strings.data()+offset,text,length);active->nextString+=length;return static_cast<int>(offset);
}
CBaseEntity& Engine::Construct(const char* classname,int index,bool named)
{
    auto ed=std::make_unique<edict_t>();ed->v.pContainingEntity=ed.get();ed->serialnumber=1;
    auto* raw=ed.get();indices[raw]=index;edicts.push_back(std::move(ed));
    raw->v.classname=AllocString(classname);
    if(named) namedPending.insert(raw);
    if(std::strcmp(classname,"playerspawn")==0) playerspawn(&raw->v);
    else if(std::strcmp(classname,"csol_playerspawn_proxy")==0) csol_playerspawn_proxy(&raw->v);
    else if(std::strcmp(classname,"zombiespawn")==0) zombiespawn(&raw->v);
    else GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&raw->v));
    return *static_cast<CBaseEntity*>(raw->pvPrivateData);
}
CBaseEntity& Engine::Add(const char* classname,int index)
{
    auto& entity=Construct(classname,index);
    entity.pev->origin=Vector(float(index),float(index+1),float(index+2));
    entity.pev->angles=Vector(float(index+3),float(index+4),float(index+5));
    entity.Spawn();++spawnCalls[entity.edict()];return entity;
}
CCSOLPlayerSpawn& Engine::Player(int index,int id,int team)
{
    auto& e=static_cast<CCSOLPlayerSpawn&>(Add("playerspawn",index));e.m_iId=id;e.m_iTeam=team;return e;
}
CCSOLMonsterSpawn& Engine::Monster(int index,int id,int maxSpawn,int team)
{
    auto& e=static_cast<CCSOLMonsterSpawn&>(Add("zombiespawn",index));e.m_iId=id;e.m_iTeam=team;e.m_iMaxSpawn=maxSpawn;return e;
}
CBaseEntity& Engine::ReplaceWithBase(edict_t* ed)
{
    orphanedPrivate.push_back(ed->pvPrivateData);ed->pvPrivateData=nullptr;
    return *GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&ed->v));
}
edict_t* Engine::Create(int classname)
{
    ++active->creates;active->calls.emplace_back("create");
    if(active->createFailure==CreateFailure::Null) return nullptr;
    auto& entity=active->Construct(active->strings.data()+classname,10000+active->creates,true);
    auto* created=entity.edict();
    if(active->createFailure==CreateFailure::PrivateMissing) {active->orphanedPrivate.push_back(entity.edict()->pvPrivateData);entity.edict()->pvPrivateData=nullptr;}
    if(active->onNamedConstruction) active->onNamedConstruction(created);
    return created;
}
void Engine::SetOrigin(edict_t* ed,const float* origin)
{
    ++active->setOrigins;ed->v.origin=Vector(origin[0],origin[1],origin[2]);
}
// ReGameDLL registers by the requested classname after the engine constructor
// callback returns. This records that ordering without replacing its hash.
edict_t* CREATE_NAMED_ENTITY(string_t classname)
{
    const std::string requested=STRING(classname);
    auto* created=g_engfuncs.pfnCreateNamedEntity(classname);
    if(created)
    {
        Engine::active->namedPending.erase(created);
        Engine::active->namedRegistrations[created]=requested;
        Engine::active->calls.emplace_back("register:"+requested);
    }
    return created;
}

// Engine lookup substitute: traversal order comes from controlled edict list.
// The real ReGameDLL classname hash and full engine allocation are not run.
CBaseEntity* UTIL_FindEntityByClassname(CBaseEntity* previous,const char* classname)
{
    auto& engine=*Engine::active;++engine.searches;
    bool eligible=previous==nullptr;
    for(auto& ed:engine.edicts)
    {
        if(!eligible) {if(ed.get()==previous->edict()) eligible=true;continue;}
        if(engine.namedPending.contains(ed.get())) continue;
        const auto registered=engine.namedRegistrations.find(ed.get());
        if(registered!=engine.namedRegistrations.end() && registered->second!=classname) continue;
        if(ed->pvPrivateData && FClassnameIs(ed.get(),classname))
        {engine.calls.emplace_back(std::string("find:")+classname+":"+std::to_string(Engine::Index(ed.get())));return static_cast<CBaseEntity*>(ed->pvPrivateData);}
    }
    engine.calls.emplace_back(std::string("end:")+classname);return nullptr;
}
// Same validity and deferred-removal semantics as current util.cpp; the
// UpdateOnRemove service is an explicit callback to simulate reentrant plugins.
BOOL UTIL_IsValidEntity(edict_t* ed) {return ed && !ed->free && !(ed->v.flags&FL_KILLME);}
void UTIL_Remove(CBaseEntity* entity)
{
    if(!entity || entity->IsPlayer() || (entity->pev->flags&FL_KILLME)) return;
    auto& engine=*Engine::active;++engine.removes;engine.removed.push_back(entity->entindex());
    engine.calls.emplace_back("remove:"+std::to_string(entity->entindex()));
    if(engine.onRemove) engine.onRemove(*entity);
    entity->pev->solid=SOLID_NOT;entity->pev->flags|=FL_KILLME;entity->pev->targetname=0;
}
// Current cbase.cpp may return -1 solely because IsAllowedToSpawn rejected it,
// with no FL_KILLME write. This fixture retains that exact observable case.
int DispatchSpawn(edict_t* ed)
{
    auto& engine=*Engine::active;++engine.dispatches;engine.calls.emplace_back("dispatch");
    auto* entity=static_cast<CBaseEntity*>(ed->pvPrivateData);
    if(entity) {entity->pev->absmin=entity->pev->origin-Vector(1,1,1);entity->pev->absmax=entity->pev->origin+Vector(1,1,1);entity->Spawn();++engine.spawnCalls[ed];}
    switch(engine.spawnFailure)
    {
    case Engine::SpawnFailure::Reject:return -1;
    case Engine::SpawnFailure::Kill:ed->v.flags|=FL_KILLME;return -1;
    case Engine::SpawnFailure::Free:ed->free=TRUE;return -1;
    case Engine::SpawnFailure::Recycle:++ed->serialnumber;return 0;
    case Engine::SpawnFailure::ChangeClass:ed->v.classname=Engine::AllocString("info_player_start");return 0;
    case Engine::SpawnFailure::PrivateMissing:engine.orphanedPrivate.push_back(ed->pvPrivateData);ed->pvPrivateData=nullptr;return 0;
    case Engine::SpawnFailure::ReplaceWithBase:engine.ReplaceWithBase(ed);return 0;
    default:return 0;
    }
}
