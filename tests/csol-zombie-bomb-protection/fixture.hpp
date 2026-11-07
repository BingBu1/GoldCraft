// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "../../native/csol/server/scenario/core/scenario_player_runtime.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_skills.h"
#include <bit>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace bomb_test
{
inline int checks{},failures{};
inline bool active=true;
inline std::uint64_t generation{};
inline csol::scenario::EquipmentTables tables;
inline void Check(bool ok,const char* reason)
{++checks;if(!ok){++failures;std::cerr<<"FAIL "<<reason<<'\n';}}
inline void Require(bool ok,const char* reason)
{Check(ok,reason);if(!ok)throw std::runtime_error(reason);}
struct Player final:CBasePlayer {void ResetMaxSpeed() override {}};
struct Weapon final:CBasePlayerWeapon
{
    std::function<void()> isWeapon;
    BOOL IsWeapon() override {auto call=isWeapon;if(call)call();return TRUE;}
};
struct Fixture
{
    inline static Fixture* current{};
    globalvars_t globals{};
    std::array<edict_t,5> edicts{};
    Player* player{};
    std::array<Weapon*,4> guns{};
    std::shared_ptr<CCSOLScenarioSession> session;
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
    std::shared_ptr<CCSOLScenarioWeaponState> weapons;
    int seeds{},rolls{},sample=99;
    std::uint32_t seed=5489;
    bool valid=true;
    std::function<void()> onSeed,onRandom;
    std::vector<int> order;
    Fixture()
    {
        Require(!current,"one controlled SDK engine");current=this;active=true;
        tables={};tables.addons.resize(14);tables.addons[13].parameters={{"Percent",50},{"PercentAdd",5}};
        gpGlobals=&globals;globals.time=10;globals.maxClients=32;globals.maxEntities=64;g_engfuncs={};
        g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t* ed,int32 bytes)->void*
        {return ed->pvPrivateData=std::calloc(1,static_cast<std::size_t>(bytes));};
        g_engfuncs.pfnIndexOfEdict=[](const edict_t* ed){return int(ed-current->edicts.data())+1;};
        g_engfuncs.pfnRandomLong=[](int32 low,int32 high)->int32
        {auto& f=*current;Check(low==0&&high==99,"addon13 uses inclusive integer RNG");++f.rolls;f.order.push_back(2);auto cb=f.onRandom;if(cb)cb();return f.sample;};
        for(auto& ed:edicts){ed.serialnumber=1;ed.v.pContainingEntity=&ed;}
        player=new(&edicts[0].v) Player;player->pev=&edicts[0].v;player->m_pEntity=nullptr;
        player->pev->health=100;player->pev->flags=FL_CLIENT;player->m_iNumSpawns=1;
        player->m_iJoiningState=JOINED;player->has_disconnected=false;
        for(int i=0;i<4;++i)
        {guns[i]=new(&edicts[i+1].v) Weapon;guns[i]->pev=&edicts[i+1].v;guns[i]->m_pEntity=nullptr;}
        player->m_pActiveItem=guns[0];player->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=guns[0];
        player->m_rgpPlayerItems[PISTOL_SLOT]=guns[1];player->m_rgpPlayerItems[KNIFE_SLOT]=guns[2];
        player->m_rgpPlayerItems[GRENADE_SLOT]=guns[3];
        session=std::make_shared<CCSOLScenarioSession>();Require(session->Precache("fixture"),"controlled resource admission");
        weapons=session->Weapons();players=std::make_shared<CCSOLScenarioPlayerRuntime>(session);
        Require(players->Activate()&&players->SetProfile(*player,{}),"controlled registry admits actual SDK player");
    }
    ~Fixture()
    {
        onSeed={};onRandom={};players.reset();weapons.reset();session.reset();
        player->~Player();for(auto* gun:guns)gun->~Weapon();
        for(auto& ed:edicts)std::free(ed.pvPrivateData);
        current=nullptr;g_engfuncs={};gpGlobals=nullptr;active=true;
    }
    EHANDLE Handle()const{return EHANDLE(player);}
    auto State()const{return players->Find(Handle());}
    CSOLMonsterSkillGuard Guard()const
    {return {[](const void* p){return static_cast<const Fixture*>(p)->valid;},this};}
    static std::uint32_t Seed()
    {auto& f=*current;++f.seeds;f.order.push_back(1);auto cb=f.onSeed;if(cb)cb();return f.seed;}
    bool Hit(float& damage,float& force,bool& immune)
    {csol::scenario::ConfigError error;return players->PrepareZombieBombHit(Handle(),Guard(),damage,force,immune,error,Seed);}
    void Level(int level)
    {auto profile=State()->profile;profile.equipment.addonLevels[13]=level;Require(players->SetProfile(*player,std::move(profile)),"profile update retains actual owner");}
    void Block(bool first,bool second)
    {auto profile=State()->profile;profile.weaponEffectsBlocked21F2=first;profile.weaponEffectsBlocked34F0=second;Require(players->SetProfile(*player,std::move(profile)),"block flags update");}
    static CSOLWeaponPartEffect Part(std::uint32_t id,double probability=100,double duration=5,double percent=25,bool passive=false,int kind=15)
    {return {id,0,0,kind,passive,{1u<<15},{probability,duration,percent}};}
    void Parts(std::vector<CSOLWeaponPartEffect> parts)
    {Require(weapons->ReplacePartEffects(std::move(parts)),"real part directory accepts records");Require(weapons->SetParts(*guns[0],{1,2}),"real entity part slots assigned");}
};
}
