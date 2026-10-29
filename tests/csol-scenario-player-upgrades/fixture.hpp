// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "precompiled.h"
#include "../../native/csol/server/scenario_player_upgrades.h"
#include "../csol-scenario-difficulty/support.hpp"
#include <functional>
#include <memory>

namespace upgrade_test
{
using difficulty_test::Check;
using difficulty_test::Require;
using namespace csol::scenario;
inline std::filesystem::path resourceRoot;
inline constexpr const char* LevelPath="resource/ZBSVER2/ZBS_neededexp.cso";
inline constexpr const char* ClassPath="resource/ZBSVER2/ZBS_class.cso";
std::shared_ptr<const UpgradeTables> RealTables();
std::array<UpgradeAttribute,5> Attributes(int cap=21);
struct Events final : UpgradeEvents
{
    bool valid{true}; int heals{},full{};
    std::array<int,5> bonus{};
    std::vector<UpgradeStat> increases;
    std::vector<std::string> order;
    std::function<void(std::string_view,UpgradeStat)> callback;
    bool Current() const override {return valid;}
    bool ReadBonus(UpgradeStat stat,int& value) override;
    bool BonusChanged(UpgradeStat stat,int,int) override;
    bool LevelIncreased(UpgradeStat stat,const UpgradeProgressState&) override;
    bool ReachedEffectiveCap(const UpgradeProgressState&) override {++full;return true;}
    bool HealAfterUpgrade() override {++heals;return true;}
};

// Actual player layout, RTTI, inline IsAlive and hooked ResetMaxSpeed/AddAccount
// are retained. Unrelated player creation/destruction services are out of scope.
struct Player final : CBasePlayer
{ void OnCreate() override {} void OnDestroy() override {} };
struct Message
{
    int destination{},type{},recipient{};
    std::string writes;
    std::vector<std::uint8_t> wire;
};
struct Engine
{
    inline static Engine* active{};
    globalvars_t globals{};
    std::unique_ptr<edict_t[]> edicts;
    std::unique_ptr<CGameRules> rules;
    std::vector<char> strings;
    std::size_t nextString{1}; int nextEntity{1}; bool open{},rejected{};
    int movementCalls{},resetHooks{};
    std::vector<Message> messages;
    std::vector<std::string> registrations,events;
    std::function<void(std::string_view)> boundary;
    std::weak_ptr<CCSOLScenarioPlayerUpgrades> movement;
    Engine(); ~Engine();
    Player& NewPlayer();
    void Destroy(Player& player);
    void Step(std::string_view label);
    static int Index(const edict_t* ed);
    static int String(const char* value);
    static void Write(int value,int bytes,char type);
};
std::unique_ptr<CGameRules> MakeRules();

// A controlled character/effect service edge, deliberately not evidence that
// production character/effect initialization has been implemented or accepted.
struct Services final : CCSOLScenarioUpgradeServices
{
    bool valid{true},jumpOk{true}; int full{},xpCalls{},jumpCalls{};
    float xpFactor{1},gravity{0.75f}; std::optional<float> money;
    std::array<int,5> bonus{};
    std::vector<int> xpInputs;
    std::function<void(std::string_view)> callback;
    std::function<void(int,std::array<UpgradeAttribute,5>&)> initialize;
    bool Current() const override {return valid;}
    bool Initialize(const EHANDLE&,int id,std::array<UpgradeAttribute,5>& attributes) override;
    bool ReadBonus(const EHANDLE&,UpgradeStat stat,int& value) override;
    bool ModifyExperience(const EHANDLE&,int& amount) override;
    bool MoneyAddPercent(const EHANDLE&,std::optional<float>& percent) override;
    bool ModifyJump(const EHANDLE&,float& value) override;
    bool ReachedEffectiveCap(const EHANDLE&) override;
    void Step(std::string_view label);
};
struct Fixture
{
    Engine engine;
    Player& player;
    CCSOLScenarioMessages messages;
    std::shared_ptr<Services> services=std::make_shared<Services>();
    std::shared_ptr<const UpgradeTables> tables;
    std::shared_ptr<CCSOLScenarioPlayerUpgrades> state;
    explicit Fixture(float ratio=1);
    void Configure(int id=0);
};
void ResourceCases(); void ProgressCases(); void ConsumerCases();
void MessageCases(); void CancellationCases();
}
