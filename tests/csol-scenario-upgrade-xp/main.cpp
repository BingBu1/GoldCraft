// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../../native/csol/server/scenario_player_upgrades.h"
#include <bit>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace csol::scenario;
int checks{};
void Check(bool value, const char* description)
{
    ++checks;
    if (!value) throw std::runtime_error(description);
}
struct Packet
{
    int destination{}, type{}, recipient{};
    std::vector<std::uint8_t> bytes;
    std::string writes;
};
struct Player final : CBasePlayer
{
    std::function<void()> speed;
    void OnCreate() override {}
    void OnDestroy() override {}
    void ResetMaxSpeed() override { if (speed) speed(); }
};
struct Services final : CCSOLScenarioUpgradeServices
{
    bool valid{true};
    int base{1}, cap{21}, reads{};
    float factor{1};
    std::array<int,5> bonus{};
    std::function<void()> callback;
    bool Current() const override { return valid; }
    bool Initialize(const EHANDLE&, int, std::array<UpgradeAttribute,5>& values) override
    { for (auto& value : values) { value.level = base; value.cap = cap; } return true; }
    bool ReadBonus(const EHANDLE&, UpgradeStat stat, int& value) override
    { ++reads; value = bonus[static_cast<unsigned>(stat)]; if (callback) callback(); return true; }
    bool ModifyExperience(const EHANDLE&, int& value) override
    { value = int(float(value) * factor); return true; }
    bool MoneyAddPercent(const EHANDLE&, std::optional<float>& value) override { value.reset(); return true; }
    bool ModifyJump(const EHANDLE&, float&) override { return true; }
    bool ReachedEffectiveCap(const EHANDLE&) override { return true; }
};
struct Fixture
{
    inline static Fixture* active{};
    globalvars_t globals{};
    std::array<edict_t, 2> edicts{};
    std::vector<Packet> packets;
    std::function<void(std::string_view)> callback;
    bool open{};
    Player* player{};
    CCSOLScenarioMessages messages;
    std::shared_ptr<Services> services = std::make_shared<Services>();
    std::shared_ptr<UpgradeTables> tables = std::make_shared<UpgradeTables>();
    std::shared_ptr<CCSOLScenarioPlayerUpgrades> state;
    Fixture(int threshold = 2000)
    {
        Check(!active, "single test engine"); active = this;
        gpGlobals = &globals; g_engfuncs = {};
        globals.maxClients = 32;
        for (auto& edict : edicts) { edict.serialnumber = 1; edict.v.pContainingEntity = &edict; }
        g_engfuncs.pfnPvAllocEntPrivateData = [](edict_t* edict, int32 bytes) -> void*
        { return edict->pvPrivateData = std::calloc(1, std::size_t(bytes)); };
        g_engfuncs.pfnIndexOfEdict = [](const edict_t* edict) { return int(edict - active->edicts.data()); };
        g_engfuncs.pfnRegUserMsg = [](const char* name, int) {
            if (!std::strcmp(name, "MPToCL")) return 96;
            if (!std::strcmp(name, "ZBSVer2")) return 97;
            if (!std::strcmp(name, "MPToCL2")) return 98;
            if (!std::strcmp(name, "CsoSkState")) return 99;
            throw std::runtime_error("unexpected message name");
        };
        g_engfuncs.pfnMessageBegin = [](int destination, int type, const float*, edict_t* recipient) {
            Check(!active->open, "non-nesting engine message"); active->open = true;
            active->packets.push_back({destination,type,int(recipient-active->edicts.data())});
            active->Step("begin");
        };
        g_engfuncs.pfnWriteByte = [](int value) { active->Write(value,1,'B'); };
        g_engfuncs.pfnWriteLong = [](int value) { active->Write(value,4,'L'); };
        g_engfuncs.pfnMessageEnd = [] {
            Check(active->open, "matching engine end"); active->open = false;
            active->packets.back().writes += 'E'; active->Step("end");
        };
        auto& edict = edicts[1]; edict.v.health = 40; edict.v.max_health = 100;
        edict.v.flags = FL_CLIENT;
        player = GetClassPtr<CCSEntity>(reinterpret_cast<Player*>(&edict.v));
        tables->levels.rows.resize(101, {threshold,{100,1,0,100,200}});
        UpgradeClass type; type.increases.fill(20);
        type.order = {UpgradeStat::Health,UpgradeStat::Damage,UpgradeStat::Speed,UpgradeStat::Armor,UpgradeStat::Ammo};
        tables->classes.byId.emplace(0,type);
        Check(messages.Register(), "real message registration");
        state = CCSOLScenarioPlayerUpgrades::Create(*player,tables,services,messages,1,1);
        Check(bool(state), "real player upgrades create");
    }
    ~Fixture()
    {
        callback = {}; services->callback = {}; state->Invalidate(); state.reset();
        delete player->m_pEntity; player->m_pEntity = nullptr; player->~Player();
        std::free(edicts[1].pvPrivateData); g_engfuncs = {}; gpGlobals = nullptr; active = nullptr;
    }
    void Step(std::string_view label) { const auto saved = callback; if (saved) saved(label); }
    void Write(int value, int count, char kind)
    {
        Check(open, "writer has packet"); auto& packet = packets.back(); packet.writes += kind;
        for (int i=0;i<count;++i) packet.bytes.push_back(std::uint8_t(std::uint32_t(value)>>(8*i)));
        Step("write");
    }
    void Configure()
    { Check(state->Configure(0,0,false), "configure real upgrades"); packets.clear(); }
    void ExpectXp(int percent)
    {
        Check(packets.size()==1, "exactly one XP message"); const auto& p=packets.front();
        Check(p.destination==MSG_ONE && p.type==97 && p.recipient==1 && p.writes=="BLE", "XP destination and wire types");
        std::vector<std::uint8_t> expected{3};
        for (int i=0;i<4;++i) expected.push_back(std::uint8_t(std::uint32_t(percent)>>(8*i)));
        Check(p.bytes==expected, "signed integer percentage bytes");
    }
};
void Timing()
{
    Fixture f;
    Check(f.state->Configure(0,0,false), "first configuration");
    Check(f.packets.size()==7 && f.packets.back().bytes==std::vector<std::uint8_t>({3,0,0,0,0}), "initial XP zero after six attribute messages");
    f.packets.clear();
    Check(f.state->Update(0), "time zero accepted");
    Check(f.services->reads==0 && f.packets.empty(), "strict first update gate");
    Check(f.state->AddExperience(19), "gain below one percent");
    Check(f.packets.empty(), "XP gain does not send HUD immediately");
    Check(f.state->Update(1), "first periodic check");
    Check(f.packets.empty(), "unchanged zero percent suppressed");
    Check(f.state->AddExperience(1), "cross one percent boundary");
    const int reads = f.services->reads;
    Check(f.state->Update(1.5f) && f.services->reads==reads && f.packets.empty(), "strict equality suppresses timed update");
    Check(f.state->Update(1.5001f), "after boundary"); f.ExpectXp(1); f.packets.clear();
    Check(f.state->Update(3) && f.packets.empty(), "duplicate percentage suppressed");
    Check(f.state->Update(3,true), "HUD reset forces replay"); f.ExpectXp(1); f.packets.clear();
    Check(f.state->Configure(0,0,false), "reconfigure keeps XP");
    Check(f.state->State()->experience==20 && f.packets.size()==6, "class switch has no fake zero reset");
    f.packets.clear(); Check(f.state->Update(4) && f.packets.empty(), "class reconfigure retains sent percentage");
    Check(!f.state->Update(std::numeric_limits<float>::quiet_NaN()), "nonfinite clock rejected");
}
void Boundaries()
{
    {
        Fixture f(1567089081); f.Configure();
        Check(f.state->AddExperience(924582528), "large exact float XP");
        Check(f.state->Update(1), "float operation boundary"); f.ExpectXp(59);
        // Double/integer arithmetic and division-before-multiply both yield58.
    }
    {
        Fixture f; f.Configure(); f.services->factor=-1;
        Check(f.state->AddExperience(40), "negative adjusted XP retained");
        Check(f.state->Update(1), "negative percentage"); f.ExpectXp(-2);
    }
    {
        Fixture f; f.Configure(); Check(f.state->AddExperience(1000), "half XP");
        Check(f.state->Update(1), "half HUD"); f.ExpectXp(50); f.packets.clear();
        Check(f.state->AddExperience(1500), "level and remainder");
        Check(f.packets.size()==2 && f.packets[0].bytes[0]==5 && f.packets[1].bytes[0]==6, "level then stat immediately without XP");
        f.packets.clear(); Check(f.state->Update(2), "post level XP"); f.ExpectXp(25);
    }
    {
        Fixture f; f.Configure(); Check(f.state->AddExperience(1000), "cap setup XP");
        Check(f.state->Update(1), "cap setup publication"); f.packets.clear();
        f.services->bonus.fill(20); Check(f.state->Update(2), "effective cap via bonuses");
        Check(f.packets.size()==7 && f.packets[0].bytes[0]==4 && f.packets[5].bytes[0]==14 &&
            f.packets[6].bytes==std::vector<std::uint8_t>({3,0,0,0,0}), "bonus total and five stats precede full-cap zero XP");
    }
    {
        Fixture f; f.services->base=21; f.Configure();
        Check(f.state->Update(1,true), "base cap replay"); f.ExpectXp(0);
    }
}
void Cancellation()
{
    for (const std::string_view edge : {"bonus","begin","write","end"})
    {
        Fixture f; f.Configure(); Check(f.state->AddExperience(20), "retirement setup");
        bool retired=false;
        const auto retire=[&] { if (!retired) { retired=true; f.state->Invalidate(); } };
        if (edge=="bonus") f.services->callback=retire;
        else f.callback=[&](std::string_view at) { if (at==edge) retire(); };
        Check(!f.state->Update(1), "retirement propagates failure");
        Check(retired && !f.state->State() && !f.open, "state retired with complete packet boundary");
        if (edge=="bonus") Check(f.packets.empty(), "retired service sends no XP");
        else f.ExpectXp(1);
        f.callback={}; f.services->callback={};
    }
    {
        Fixture f; f.Configure(); Check(f.state->AddExperience(20), "failed sender setup");
        const auto writer=g_engfuncs.pfnWriteLong; g_engfuncs.pfnWriteLong=nullptr;
        Check(!f.state->Update(1) && f.packets.empty(), "unavailable writer rejects before message");
        g_engfuncs.pfnWriteLong=writer;
        Check(f.state->Update(2), "failed sender does not poison duplicate cache"); f.ExpectXp(1);
    }
    {
        Fixture f; f.Configure(); Check(f.state->AddExperience(20), "reentry setup");
        bool attempted=false;
        f.callback=[&](std::string_view at) {
            if (at=="begin" && !attempted) { attempted=true;
                Check(!f.state->Update(1,true) && !f.state->Configure(0,0,false) && !f.state->Publish(), "callbacks cannot reenter HUD state");
            }
        };
        Check(f.state->Update(1) && attempted, "outer update survives rejected reentry"); f.ExpectXp(1);
    }
}
}
bool CSOL_RulesRejected() { return false; }
int main()
{
    try { Timing(); Boundaries(); Cancellation(); }
    catch (const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
    std::cout<<"checks="<<checks<<" failures=0\n"; return 0;
}
