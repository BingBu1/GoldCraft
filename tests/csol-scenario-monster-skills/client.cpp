// SPDX-License-Identifier: GPL-3.0-or-later
// Links the production scenario_effects.cpp. Only engine/render boundaries are controlled.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/scenario_effects.hpp"
#include "../../native/csol/scenario_monster_effects.hpp"
#include "../../native/csol/client/frozen_death.hpp"
#include "../../native/csol/client/frozen_smoke.hpp"
#include "../../native/csol/client/oberon_hint.hpp"
#include "../../native/include/goldcraft/vitals_protocol.hpp"
#include <r_efx.h>
#include <event_api.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <source_location>
#include <string_view>

#ifdef CSOL_FX_PRE_DEFERRED_HEALTH
// The negative-control source predates this export. Initialization must fail
// the native-handler identity assertion before this no-op could be reached.
namespace csol::client { void ScenarioEffectsFrame() {} }
#endif

namespace fixture
{
using Vec = std::array<float, 3>;
struct Draw { int sprite, frame, x, y; std::array<int, 3> color; };
std::map<std::string, pfnUserMsgHook> receivers;
std::vector<std::string> calls, spriteLoads;
std::vector<Draw> draws;
std::array<int, 3> tint{};
int checks{}, forwards{}, syncs{}, spriteHandle{}, spriteIndex{41};
int healthHooks{}, healthMessages{}, wideHealthValue{-1};
bool wideHud{};
void* forwardedHealthData{};
int forwardedHealthSize{};
int width{800}, height{600};
float now{10};
bool allocationFails{}, spriteLoadFails{}, resetOnSync{};
TEMPENTITY temp{};
efx_api_t fx{};
event_api_t events{};
Vec spriteOrigin{}, soundOrigin{};
float fps{}, scale{};
int explodeFlags{};
std::string expectedModel{"sprites/deimosexp.spr"};
void Check(bool value, const std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error("monster-fx-client line " + std::to_string(where.line()));
}
bool Near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
int Previous(const char*, int, void*) { ++forwards; return 73; }
int WideHealth(const char* name, int size, void* data)
{
    Check(std::string_view(name) == "Health");
    ++healthMessages; forwardedHealthData = data; forwardedHealthSize = size;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (goldcraft::vitals::valid_payload(bytes, size, 1, false))
    {
        if (size == 1) wideHealthValue = bytes[0];
        else std::memcpy(&wideHealthValue, bytes, sizeof(wideHealthValue));
    }
    return 73;
}
int Send(const char* name, std::vector<unsigned char> bytes)
{
    const auto it = receivers.find(name);
    Check(it != receivers.end());
    return it->second(name, static_cast<int>(bytes.size()), bytes.empty() ? nullptr : bytes.data());
}
std::vector<unsigned char> Packet(int type, Vec origin)
{
    std::vector<unsigned char> bytes{static_cast<unsigned char>(type)};
    for (float value : origin)
    {
        const auto raw = static_cast<std::uint16_t>(static_cast<std::int16_t>(value*8));
        bytes.push_back(static_cast<unsigned char>(raw));
        bytes.push_back(static_cast<unsigned char>(raw >> 8));
    }
    return bytes;
}
void Effect(int type, Vec origin) { Check(Send("CsoMonFX", Packet(type, origin)) == 1); }
void Paint(float advance = 0, int intermission = 0)
{
    now += advance; draws.clear();
    csol::client::DrawScenarioEffects(now, intermission);
}
void Clean()
{
    csol::client::ResetScenarioEffects();
    Check(Send("Health", {100}) == 73);
    calls.clear(); draws.clear(); spriteLoads.clear();
    now = 10; width = 800; height = 600;
    spriteIndex = 41; allocationFails = spriteLoadFails = resetOnSync = false;
    csol::client::view = {};
    csol::client::view.forward[0] = 1;
    csol::client::view.right[1] = 1;
    csol::client::view.simorg[0] = 100;
    Paint(); // The HUD clock advances even without a feedback packet.
}
void Install(bool withWideHud = false)
{
    using namespace csol::client;
    wideHud = withWideHud;
    static metahook_api_t host;
    api = &host;
    for (const char* name : {"MPToCL", "InitHUD", "ResetHUD", "Health", "HideWeapon"})
        receivers[name] = Previous;
    host.HookUserMsg = [](const char* name, pfnUserMsgHook callback) -> pfnUserMsgHook
    {
        if (std::string_view(name) == "Health") ++healthHooks;
        const auto found = receivers.find(name);
        if (found == receivers.end()) return nullptr;
        const auto previous = found->second; found->second = callback; return previous;
    };
    engine.pfnHookUserMsg = [](const char* name, pfnUserMsgHook callback)
    { receivers[name] = callback; return 1; };
    engine.GetClientTime = [] { return now; };
    engine.pEfxAPI = &fx;
    engine.pEventAPI = &events;
    events.EV_FindModelIndex = [](const char* name)
    { Check(std::string(name) == expectedModel); calls.emplace_back("model"); return spriteIndex; };
    fx.R_DefaultSprite = [](float* origin, int model, float rate) -> TEMPENTITY*
    {
        Check(model == 41); calls.emplace_back("sprite");
        std::copy_n(origin, 3, spriteOrigin.begin()); fps = rate; temp = {};
        std::copy_n(origin, 3, temp.entity.origin);
        return allocationFails ? nullptr : &temp;
    };
    fx.R_Sprite_Explode = [](TEMPENTITY* value, float factor, int flags)
    {
        Check(value == &temp); calls.emplace_back("explode"); scale = factor; explodeFlags = flags;
        value->entity.origin[2] += 10; // Real stock API changes only temp origin.
    };
    events.EV_PlaySound = [](int entity, float* origin, int channel, const char* sample,
        float volume, float attenuation, int flags, int pitch)
    {
        calls.emplace_back("sound"); std::copy_n(origin, 3, soundOrigin.begin());
        Check(entity == -1 && channel == CHAN_AUTO && std::string(sample) == "zombi/Zombi_Bomb_Exp.wav");
        Check(volume == 1 && attenuation == 0.3f && flags == 0 && pitch == 100);
    };
    engine.pfnGetScreenInfo = [](SCREENINFO* info)
    { Check(info->iSize == sizeof(*info)); info->iWidth = width; info->iHeight = height; return 1; };
    engine.pfnSPR_Load = [](const char* path)
    { spriteLoads.emplace_back(path); return spriteLoadFails ? 0 : std::string(path).find("320") != std::string::npos ? 320 : 640; };
    engine.pfnSPR_Width = [](int handle, int frame)
    { return (frame == 0 || frame == 2 ? 128 : 48) / (handle == 320 ? 2 : 1); };
    engine.pfnSPR_Height = [](int handle, int frame)
    { return (frame == 0 || frame == 2 ? 48 : 128) / (handle == 320 ? 2 : 1); };
    engine.pfnSPR_Set = [](int handle, int r, int g, int b)
    { spriteHandle = handle; tint = {r, g, b}; };
    engine.pfnSPR_DrawAdditive = [](int frame, int x, int y, const wrect_t* rect)
    { Check(rect == nullptr); draws.push_back({spriteHandle, frame, x, y, tint}); };
    // The actual plugin chain is GoldCraft -> CSOL -> native HUD_Init.
    // The strict GoldCraft check runs only after CSOL's initializer returns.
    const auto csolInit = [] { InitializeScenarioEffects(); InitializeScenarioEffects(); };
    const auto goldcraftInit = [&]
    {
        csolInit();
        Check(receivers.at("Health") == Previous && healthHooks == 0);
        receivers["Health"] = WideHealth;
    };
    if (wideHud) goldcraftInit();
    else
    {
        csolInit();
        Check(receivers.at("Health") == Previous && healthHooks == 0);
    }
    ScenarioEffectsFrame(); ScenarioEffectsFrame();
    Check(healthHooks == 1);
}
void HealthLifecycle()
{
    using namespace csol::client;
    constexpr Vec nearSource{100,0,0};
    const auto packet = [](std::uint32_t value)
    {
        return std::vector<unsigned char>{static_cast<unsigned char>(value),
            static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value >> 16),
            static_cast<unsigned char>(value >> 24)};
    };
    const auto paintHealth = [&](std::vector<unsigned char> bytes, int value)
    {
        ResetScenarioEffects(); Effect(2,nearSource);
        const int before = healthMessages;
        Check(Send("Health",bytes)==73); Paint();
        if (wideHud) Check(wideHealthValue==value && healthMessages==before+1);
        Check(draws.size()==(value==0 ? 0u : 4u));
        if (value>0) Check(draws[0].color == (value>25 ? std::array<int,3>{255,160,0} : std::array<int,3>{250,0,0}));
    };
    Clean();
    for (unsigned char value : {0,25,26,255}) paintHealth({value},value);
    if (wideHud)
        for (std::uint32_t value : {0u,25u,26u,256u,1000u,0x1000019u,0x7fffffffu})
            paintHealth(packet(value),static_cast<int>(value));
    // Unknown new-life health must not infer alive from stale/default HP.
    for (const char* reset : {"ResetHUD","InitHUD"})
    {
        Check(Send(reset,{})==73); Effect(2,nearSource); Paint(); Check(draws.empty());
        Check(Send("Health",{0})==73); Paint(); Check(draws.empty());
        Check(Send("Health",{26})==73); Paint(); Check(draws.size()==4);
    }
    if (wideHud)
    {
        paintHealth({25},25);
        for (auto invalid : std::vector<std::vector<unsigned char>>{{},{1,2},{1,2,3},{1,2,3,4,5},packet(0x80000000u),packet(0xffffffffu)})
        {
            const int before=healthMessages;
            Check(Send("Health",invalid)==73); Paint();
            Check(wideHealthValue==25 && healthMessages==before+1);
            Check(draws.size()==4 && draws[0].color==std::array<int,3>{250,0,0});
        }
        const auto receiver=receivers.at("Health");
        const int before=healthMessages;
        Check(receiver("Health",4,nullptr)==73 && healthMessages==before+1);
        Check(forwardedHealthData==nullptr && forwardedHealthSize==4);
        auto unaligned=packet(1000); unaligned.insert(unaligned.begin(),0x5a);
        Check(receiver("Health",4,unaligned.data()+1)==73);
        Check(forwardedHealthData==unaligned.data()+1 && forwardedHealthSize==4 && wideHealthValue==1000);
        Paint(); Check(draws.size()==4 && draws[0].color==std::array<int,3>{255,160,0});
    }
    const auto observer=receivers.at("Health");
    const int beforeHooks=healthHooks;
    InitializeScenarioEffects(); InitializeScenarioEffects();
    Check(receivers.at("Health")==observer);
    ScenarioEffectsFrame(); ScenarioEffectsFrame(); Check(healthHooks==beforeHooks+1);
    paintHealth({26},26); // Existing observer must retain its previous handler.
    // A fresh native HUD_Init may recreate the table in the same DLL lifetime.
    receivers["Health"]=Previous;
    InitializeScenarioEffects(); Check(receivers.at("Health")==Previous);
    if (wideHud) receivers["Health"]=WideHealth;
    ScenarioEffectsFrame(); Check(healthHooks==beforeHooks+2);
    paintHealth({25},25);
    ShutdownScenarioEffects(); Effect(2,nearSource); Paint(); Check(draws.empty());
    const int shutdownHooks=healthHooks;
    InitializeScenarioEffects(); ShutdownScenarioEffects(); ScenarioEffectsFrame();
    Check(healthHooks==shutdownHooks);
    InitializeScenarioEffects(); ScenarioEffectsFrame(); paintHealth({26},26);
    // Reset does not schedule installation or alter the message chain.
    const int finalHooks=healthHooks;
    ResetScenarioEffects(); ScenarioEffectsFrame(); Check(healthHooks==finalHooks);
}
void Impact(int effect, const char* model)
{
    Clean();
    expectedModel = model;
    Effect(effect, {-123.25f, 4095.875f, -70.125f});
    Check(calls == std::vector<std::string>({"model", "sprite", "explode", "sound"}));
    Check(spriteOrigin == Vec{-123.25f, 4095.875f, -50.125f} && soundOrigin == spriteOrigin);
    Check(temp.entity.origin[2] == -40.125f && fps == 30 && scale == 2.5f && explodeFlags == 0);
    for (const Vec origin : {Vec{-4096,-4096,-4096}, Vec{4095.875f,4095.875f,4095.875f}, Vec{-.125f,0,.125f}})
    {
        calls.clear(); Effect(effect, origin);
        Check(calls == std::vector<std::string>({"model", "sprite", "explode", "sound"}));
        Check(spriteOrigin == Vec{origin[0], origin[1], origin[2] + 20} && soundOrigin == spriteOrigin);
        Check(temp.entity.origin[2] == origin[2] + 30);
    }
    calls.clear(); auto bytes = Packet(effect, {1,2,3});
    for (int size : {0,1,2,3,4,5,6,8,9})
    { auto invalid = bytes; invalid.resize(size); Send("CsoMonFX", invalid); }
    Effect(0, {1,2,3}); Effect(4, {1,2,3}); Effect(255, {1,2,3});
    Check(calls.empty());
    Check(receivers.at("CsoMonFX")("CsoMonFX", 7, nullptr) == 1 && calls.empty());
    spriteIndex = 0; Effect(effect, {1,2,3});
    Check(calls == std::vector<std::string>({"model", "sound"}));
    calls.clear(); spriteIndex = 41; allocationFails = true; Effect(effect, {1,2,3});
    Check(calls == std::vector<std::string>({"model", "sprite", "sound"}));
    calls.clear(); csol::client::engine.pEfxAPI = nullptr; Effect(effect, {1,2,3});
    Check(calls == std::vector<std::string>({"sound"}));
    csol::client::engine.pEfxAPI = &fx;
    calls.clear(); csol::client::engine.pEventAPI = nullptr; Effect(effect, {1,2,3}); Check(calls.empty());
    csol::client::engine.pEventAPI = &events;
    allocationFails = false;
    const auto completeFx = fx;
    const auto completeEvents = events;
    for (int missing = 0; missing < 4; ++missing)
    {
        calls.clear(); fx = completeFx; events = completeEvents;
        if (missing == 0) fx.R_DefaultSprite = nullptr;
        if (missing == 1) fx.R_Sprite_Explode = nullptr;
        if (missing == 2) events.EV_FindModelIndex = nullptr;
        if (missing == 3) events.EV_PlaySound = nullptr;
        Effect(effect, {1,2,3});
        Check(calls == (missing == 3 ? std::vector<std::string>{"model", "sprite", "explode"} : std::vector<std::string>{"sound"}));
    }
    fx = completeFx; events = completeEvents;
}
void Layout()
{
    Clean(); Effect(2, {100,0,0}); Paint(); Check(calls.empty() && draws.size() == 4);
    // Original 2A4D2C0 positions, using actual per-frame resource dimensions.
    constexpr int positions[4][2]{{336,156},{496,236},{336,396},{256,236}};
    for (int i = 0; i < 4; ++i)
    {
        Check(draws[i].frame == i && draws[i].sprite == 640);
        Check(draws[i].x == positions[i][0] && draws[i].y == positions[i][1]);
        Check(draws[i].color == std::array<int,3>{255,160,0}); // Host amber, not verified CSO default.
    }
    Paint(0.125f); Check(draws.size() == 4 && draws[0].color[0] == 255);
    Paint(); Check(draws.size() == 4 && draws[0].color == std::array<int,3>{191,119,0});
    Paint(0.25f); Paint(); Check(draws.empty());
    Clean(); width = 639; height = 479; Effect(2, {100,0,0}); Paint();
    constexpr int compactPositions[4][2]{{287,167},{367,207},{287,287},{247,207}};
    Check(spriteLoads == std::vector<std::string>{"sprites/320_pain.spr"});
    for (int i = 0; i < 4; ++i)
        Check(draws[i].sprite == 320 && draws[i].x == compactPositions[i][0] && draws[i].y == compactPositions[i][1]);
    width = 640; height = 480; Paint(); Check(spriteLoads.back() == "sprites/640_pain.spr");
    Clean(); Effect(2, {150,0,0}); Paint(); Check(draws.size() == 4); // Closed 50-unit near sphere.
    Clean(); Effect(2, {150.125f,0,0}); Paint(); Check(draws.size() == 1 && draws[0].frame == 0);
}
void MonsterHits()
{
    Clean();
    const auto oldFind=events.EV_FindModelIndex;
    int bloodCount=0,sparkCount=0;Vec hit{};float amount{};
    // Noncapturing API callbacks use an explicit test observation cell.
    struct Capture { int blood{},spark{};Vec position{};int color{},spray{},drop{},count{},low{},high{};float amount{}; };
    static Capture capture;capture={};
    events.EV_FindModelIndex=[](const char* name){if(std::string_view(name)=="sprites/bloodspray.spr")return 21;if(std::string_view(name)=="sprites/blood.spr")return 22;throw std::runtime_error("unexpected hit sprite");};
    fx.R_BloodSprite=[](float* p,int color,int spray,int drop,float amount){++capture.blood;std::copy_n(p,3,capture.position.begin());capture.color=color;capture.spray=spray;capture.drop=drop;capture.amount=amount;};
    fx.R_SparkEffect=[](float* p,int count,int low,int high){++capture.spark;std::copy_n(p,3,capture.position.begin());capture.count=count;capture.low=low;capture.high=high;};
    auto blood=Packet(4,{12.125f,-34.25f,56.5f});blood.insert(blood.begin()+1,4);
    auto spark=Packet(5,{12.125f,-34.25f,56.5f});spark.insert(spark.begin()+1,16);
    Check(Send("CsoMonFX",blood)==1);Check(capture.blood==1&&capture.spark==0&&capture.position==Vec{12.125f,-34.25f,56.5f});
    Check(capture.color==247&&capture.spray==21&&capture.drop==22&&capture.amount==4);
    Check(calls.empty()); // No old explosion sprite/audio/+20 offset.
    Check(Send("CsoMonFX",spark)==1);Check(capture.spark==1&&capture.count==16&&capture.low==0&&capture.high==500);
    for(auto packet:{blood,spark}){
        for(int n=1;n<8;++n)Check(Send("CsoMonFX",std::vector<unsigned char>(packet.begin(),packet.begin()+n))==1);
        packet.push_back(0);Check(Send("CsoMonFX",packet)==1);
    }
    Check(capture.blood==1&&capture.spark==1&&calls.empty());
    events.EV_FindModelIndex=oldFind;
}
void DirectionsAndLifecycle()
{
    constexpr Vec sources[]{{200,0,0},{100,100,0},{-100,0,0},{100,-100,0}};
    for (int i = 0; i < 4; ++i)
    { Clean(); Effect(2,sources[i]); Paint(); Check(draws.size() == 1 && draws[0].frame == i); }
    Clean(); Effect(2,{200,100,0}); Paint(); Check(draws.size()==2 && draws[0].frame==0 && draws[1].frame==1);
    Check(draws[0].color[0] == 180 && draws[1].color[0] == 180);
    Effect(2,{0,0,0}); Paint(); Check(draws.empty());
    Clean(); Effect(2,{100,0,100}); Paint(); Check(draws.empty()); // Outside near sphere, no planar projection.
    Clean(); Effect(2,{100,0,0}); Check(Send("Health",{25})==73); Paint();
    Check(draws.size()==4 && draws[0].color==std::array<int,3>{250,0,0});
    Check(Send("Health",{26})==73); Paint(); Check(draws[0].color==std::array<int,3>{255,160,0});
    for (int mask : {4,8,12})
    { Check(Send("HideWeapon",{static_cast<unsigned char>(mask)})==73); Paint(.125f); Check(draws.empty()); }
    Check(Send("HideWeapon",{0})==73); Paint(); Check(draws.size()==4);
    Paint(.125f,1); Check(draws.empty()); Paint(); Check(draws.size()==4);
    Check(Send("Health",{0})==73); Paint(); Check(draws.empty());
    Check(Send("ResetHUD",{})==73); Paint(); Check(draws.empty());
    Effect(2,{100,0,0}); Paint(); Check(draws.empty());
    Check(Send("Health",{100})==73); Paint(); Check(draws.size()==4);
    Check(Send("InitHUD",{})==73); Paint(); Check(draws.empty());
    Effect(2,{100,0,0}); resetOnSync=true;
    Effect(255,{1,2,3}); Paint(); Check(draws.empty()); // Actual reset through simulated session boundary.
    Effect(2,{100,0,0}); csol::client::ShutdownScenarioEffects(); Paint(); Check(draws.empty());
    Clean(); spriteLoadFails=true; Effect(2,{100,0,0}); Paint(); Check(draws.empty());
    spriteLoadFails=false; Paint(); Check(draws.size()==4);
    Clean(); Effect(2,{100,0,0}); Paint(.375f); Paint(); Check(draws.empty());
    Check(forwards>0 && syncs>0);
}
}
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
// Unrelated effect owners are explicit link boundaries in this explosion /
// trap suite. Their decoders and rendering are covered by their own suites.
void InitializeFrozenDeath() {}
void ResetFrozenDeath() {}
bool ConsumeFrozenSmoke(int, const void*) { throw std::runtime_error("unexpected frozen-smoke packet"); }
void ResetFrozenSmoke() {}
void FrozenSmokeFrame() {}
bool ConsumeOberonHint(int, const void*) { throw std::runtime_error("unexpected Oberon hint packet"); }
void ResetOberonHints() {}
void SynchronizeSession()
{
    ++fixture::syncs;
    if (fixture::resetOnSync) { fixture::resetOnSync=false; ResetScenarioEffects(); }
}
}
int main(int argc, char** argv)
{
    try
    {
        const std::string_view mode=argc>1 ? argv[1] : "";
        fixture::Check(mode.empty() || mode=="--health" || mode=="--health-legacy");
        fixture::Install(mode=="--health");
        if (!mode.empty()) fixture::HealthLifecycle();
        else
        {
            fixture::Impact(csol::monster_effects::Impact, "sprites/deimosexp.spr");
            fixture::Impact(csol::monster_effects::ZombieBomb, "sprites/zombiebomb.spr");
            fixture::MonsterHits(); fixture::Layout(); fixture::DirectionsAndLifecycle();
        }
        std::cout << "monster-fx-client " << fixture::checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << " after " << fixture::checks << " checks\n"; return 1; }
}
