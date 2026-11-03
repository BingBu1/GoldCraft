// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "../../native/csol/server/scenario_zombie_bomb.h"
#include "../../native/csol/server/scenario_player_impulse.h"
#include "../../native/csol/server/scenario_player_runtime.h"
#include "../../native/csol/server/scenario_monster_skills.h"
#include "../../native/csol/server/scenario_runtime.h"
#include "../../native/csol/server/game_rules_gate.h"
#include <bit>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace bomb_damage_test
{
inline int checks{},failures{};
inline bool active=true,sdkCurrent=true;
inline std::shared_ptr<CCSOLScenarioPlayerRuntime> authority;
inline void Check(bool ok,const char* reason)
{++checks;if(!ok){++failures;std::cerr<<"FAIL "<<reason<<'\n';}}
inline void Require(bool ok,const char* reason)
{Check(ok,reason);if(!ok)throw std::runtime_error(reason);}
inline auto Bits(float value){return std::bit_cast<std::uint32_t>(value);}
inline bool Same(const Vector& a,const Vector& b)
{return Bits(a.x)==Bits(b.x)&&Bits(a.y)==Bits(b.y)&&Bits(a.z)==Bits(b.z);}
struct Hit {entvars_t* inflictor{};entvars_t* attacker{};float amount{};int bits{};};
struct Player final:CBasePlayer
{
    std::vector<Hit> hits;
    std::function<void()> onDamage,onAlive,onPlayer;
    BOOL damageResult=TRUE;
    BOOL TakeDamage(entvars_t* i,entvars_t* a,float d,int b)override
    {hits.push_back({i,a,d,b});auto cb=onDamage;if(cb)cb();return damageResult;}
    BOOL IsAlive()override {auto cb=onAlive;if(cb)cb();return CBasePlayer::IsAlive();}
    BOOL IsPlayer()override {auto cb=onPlayer;if(cb)cb();return CBasePlayer::IsPlayer();}
};
struct Target final:CBaseEntity
{
    int classification{},classified{},playerCalls{};
    std::vector<Hit> hits;
    std::function<void()> onClassify,onDamage,onPlayer;
    BOOL playerResult=FALSE,damageResult=TRUE;
    int Classify()override {++classified;auto cb=onClassify;if(cb)cb();return classification;}
    BOOL IsPlayer()override {++playerCalls;auto cb=onPlayer;if(cb)cb();return playerResult;}
    BOOL TakeDamage(entvars_t* i,entvars_t* a,float d,int b)override
    {hits.push_back({i,a,d,b});auto cb=onDamage;if(cb)cb();return damageResult;}
};
struct Fixture
{
    inline static Fixture* current{};
    globalvars_t globals{};
    std::array<edict_t,16> edicts{};
    Player* player{};
    Target* bomb{};Target* creator{};
    std::vector<Target*> targets;
    std::vector<edict_t*> sphere;
    std::size_t sphereCursor{};
    EHANDLE creatorHandle;
    std::shared_ptr<CCSOLScenarioRuntime> runtime;
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    int prepared{},shake{},impact{},sphereCalls{};
    bool sourceValid=true,creatorLease=true,prepareResult=true,immune=false;
    bool shakeResult=true,impactResult=true;
    float damageFactor=1,forceFactor=1;
    std::function<void()> onPrepare,onShake,onImpact,onSphere;
    std::vector<int> order;
    Fixture()
    {
        Require(!current,"one controlled SDK engine");current=this;active=true;sdkCurrent=true;
        gpGlobals=&globals;globals.time=10;globals.maxClients=1;globals.maxEntities=int(edicts.size());g_engfuncs={};
        g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t* e,int32 n)->void*
        {return e->pvPrivateData=std::calloc(1,static_cast<std::size_t>(n));};
        g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return int(e-current->edicts.data());};
        g_engfuncs.pfnPEntityOfEntIndex=[](int i)->edict_t*
        {return i>=0&&i<int(current->edicts.size())?&current->edicts[i]:nullptr;};
        g_engfuncs.pfnEntOffsetOfPEntity=[](const edict_t* e){return int(e-current->edicts.data());};
        g_engfuncs.pfnFindEntityInSphere=[](edict_t* previous,const float* center,float radius)->edict_t*
        {
            auto& f=*current;++f.sphereCalls;Check(radius==250&&center[2]==0,"engine sphere receives radius250 and shifted center");
            if(f.sphereCursor)Check(previous==f.sphere[f.sphereCursor-1],"sphere cursor retains engine order");
            auto cb=f.onSphere;if(cb)cb();return f.sphereCursor<f.sphere.size()?f.sphere[f.sphereCursor++]:nullptr;
        };
        for(auto& e:edicts){e.serialnumber=1;e.v.pContainingEntity=&e;}
        player=new(&edicts[1].v)Player;player->pev=&edicts[1].v;player->m_pEntity=nullptr;
        player->pev->health=100;player->pev->flags=FL_CLIENT|FL_ONGROUND;player->pev->origin=Vector(100,0,0);
        player->m_iNumSpawns=1;player->has_disconnected=false;player->m_iJoiningState=JOINED;
        bomb=NewTarget(2);bomb->pev->origin=Vector(0,0,20);creator=NewTarget(3);creator->classification=17;creatorHandle=EHANDLE(creator);
        runtime=std::make_shared<CCSOLScenarioRuntime>();session=std::make_shared<CCSOLScenarioSession>();
        players=std::make_shared<CCSOLScenarioPlayerRuntime>(session);runtime->session=session;runtime->players=players;
        Require(players->Activate()&&players->SetProfile(*player,{}),"controlled registry admits actual SDK player");authority=players;
    }
    ~Fixture()
    {
        authority.reset();runtime.reset();players.reset();session.reset();
        player->~Player();for(auto* t:targets)t->~Target();
        for(auto& e:edicts)std::free(e.pvPrivateData);
        current=nullptr;g_engfuncs={};gpGlobals=nullptr;active=true;sdkCurrent=true;
    }
    Target* NewTarget(int slot)
    {auto* p=new(&edicts[slot].v)Target;p->pev=&edicts[slot].v;p->m_pEntity=nullptr;p->pev->takedamage=DAMAGE_YES;targets.push_back(p);return p;}
    EHANDLE Handle()const{return EHANDLE(player);}
    auto State()const{return std::const_pointer_cast<CSOLScenarioPlayerRuntimeState>(players->Find(Handle()));}
    CSOLZombieBombHitSource Source()const
    {
        return {[](const void* p)->CBaseEntity*{const auto& f=*static_cast<const Fixture*>(p);return f.sourceValid?f.bomb:nullptr;},
            [](const void* p)->CBaseEntity*{const auto& f=*static_cast<const Fixture*>(p);return f.creatorLease?f.creatorHandle.Get<CBaseEntity>():nullptr;},this};
    }
    bool Run(float damage=100)
    {csol::scenario::ConfigError error;const bool result=CSOL_ApplyZombieBombDamage(runtime,Source(),damage,error);lastError=error.message;return result;}
    bool Impulse(const Vector& requested,bool modifiers=true,bool ordinary=false)
    {csol::scenario::ConfigError error;bool applied{};bool ok=ordinary?CSOL_ApplyScenarioPlayerImpulse(session,Handle(),requested,applied,error):
        CSOL_ApplyZombieBombPlayerImpulse(session,Handle(),requested,modifiers,applied,error);lastApplied=applied;lastError=error.message;return ok;}
    std::string lastError;bool lastApplied{};
    void Cancel(int kind)
    {
        switch(kind)
        {
        case 0:players->PlayerSpawnStarted(Handle());break;
        case 1:++player->m_iNumSpawns;break;
        case 2:player->has_disconnected=true;break;
        case 3:++edicts[1].serialnumber;break;
        case 4:player->pev->flags|=FL_KILLME;break;
        case 5:player->pev->flags|=FL_DORMANT;break;
        case 6:session->Map().BeginMap("controlled-next-round");break;
        case 7:session->RevokeEntityAccess();break;
        case 8:runtime->session=std::make_shared<CCSOLScenarioSession>();break;
        case 9:runtime->players.reset();break;
        case 10:active=false;break;
        case 11:sourceValid=false;break;
        case 12:sdkCurrent=false;break;
        case 13:runtime->RequestClose();break;
        case 14:authority.reset();break;
        }
    }
};
}
