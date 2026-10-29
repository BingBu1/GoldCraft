// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "../../native/csol/server/scenario_breakable.h"
#include "../../native/csol/server/scenario_breakable_guard.h"
#include "../../native/csol/server/scenario_wall_rewards.h"
#include "../csol-scenario-difficulty/support.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wall_test
{
using difficulty_test::Check;
using difficulty_test::Require;
struct Sink final : CBaseEntity
{
    int calls{}; USE_TYPE lastType{}; EHANDLE lastActivator, lastCaller;
    std::function<void()> callback;
    void Use(CBaseEntity* activator,CBaseEntity* caller,USE_TYPE type,float) override
    { ++calls;lastType=type;lastActivator=activator;lastCaller=caller;const auto f=callback;if(f)f(); }
};
struct Master final : CBaseEntity
{
    bool enabled{},master{true};int calls{};std::function<void()> callback;
    int ObjectCaps() override {return master?FCAP_MASTER:0;}
    BOOL IsTriggered(CBaseEntity*) override {++calls;const auto f=callback;if(f)f();return enabled;}
};
struct Actor final : CBaseEntity
{
    int classification{17};std::function<void()> callback;
    int Classify() override {const auto f=callback;if(f)f();return classification;}
};
// Real CBasePlayer layout, RTTI, IsPlayer and AddAccount are retained. Full
// gameplay player creation/destruction is outside this component boundary.
struct Player final : CBasePlayer
{
    void OnCreate() override {}
    void OnDestroy() override {}
};
struct Engine
{
    inline static Engine* active{};
    static constexpr int capacity=256;
    globalvars_t globals{};
    std::unique_ptr<edict_t[]> edicts;
    std::vector<char> strings;
    std::size_t nextString{1};int nextEntity{1},removes{};
    std::vector<std::string> events;
    struct Message {int destination{},type{},recipient{};std::vector<int> integers;std::vector<float> coordinates;};
    std::vector<Message> messages;
    std::vector<std::function<void(CBaseEntity*)>> destructors;
    std::function<void(std::string_view,CBaseEntity*)> boundary;
    bool inMessage{};
    Engine();~Engine();
    edict_t* Raw(const char* classname);
    void Destroy(edict_t* ed);
    void Step(std::string_view label,CBaseEntity* entity=nullptr);
    template<class T> T& New(const char* classname)
    {
        auto* ed=Raw(classname);auto* entity=GetClassPtr<CCSEntity>(reinterpret_cast<T*>(&ed->v));
        destructors[Index(ed)]=[](CBaseEntity* e){static_cast<T*>(e)->~T();};return *entity;
    }
    CCSOLScenarioBreakable& Wall(const char* human="1",const char* monster="0",float health=1000,int flags=0,Materials material=matWood);
    Sink& Target(const char* name);
    CTriggerRelay& Relay(const char* name,const char* target,float delay=0,bool once=false);
    std::vector<edict_t*> Delays() const;
    void Think(edict_t* ed);
    static void KV(CBaseEntity& entity,const char* key,const char* value);
    static int Index(const edict_t* ed);
    static int String(const char* text);
    static edict_t* Find(edict_t* previous,const char* key,const char* value);
};
struct Rules final : CCSOLScenarioWallRules
{
    std::uint8_t level{1},feature{};std::int8_t monsterLevel{1};bool smallCap{};
    std::vector<float> credited;std::vector<std::string> calls;
    std::function<void(std::string_view)> callback;
    bool accept{true};
    bool PlayerDamage(const EHANDLE&,float input,float ratio,float& output) override;
    bool MonsterDamage(float input,float ratio,float& output) override;
    bool CreditPlayerDamage(const EHANDLE&,float damage) override;
};
struct Players final : CCSOLScenarioWallPlayers
{
    std::shared_ptr<CSOLScenarioWallPlayer> state=std::make_shared<CSOLScenarioWallPlayer>();
    std::vector<std::string> order;std::vector<int> xp;
    std::function<void(std::string_view)> callback;
    bool missing{},moneyOk{true},xpOk{true};int extraMoney{};
    std::shared_ptr<CSOLScenarioWallPlayer> Find(const EHANDLE&) override;
    bool ModifyMoney(const EHANDLE&,int& amount) override;
    bool AddExperience(const EHANDLE&,int amount) override;
};
struct RewardFixture
{
    difficulty_test::Files files;
    csol::scenario::RoundTable rounds;
    csol::scenario::DifficultyResources difficulty;
    std::shared_ptr<Players> players=std::make_shared<Players>();
    std::shared_ptr<CCSOLScenarioWallRewards> rewards;
    explicit RewardFixture(Player& player,float moneyRatio=1,float xpRatio=1);
};
void NumericCases();void MetadataCases();void EntityCases();void DamageCases();
void TargetCases();void DelayCases();void CancellationCases();void RewardCases();
}
